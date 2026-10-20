"""Reproducible on-board Manchester experiment. Writes only inside this project."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'measurements'
OCD = '/home/yousef/zephyr-sdk-0.17.4/sysroots/x86_64-pokysdk-linux/usr/bin/openocd'
SCRIPTS = '/home/yousef/zephyr-sdk-0.17.4/sysroots/x86_64-pokysdk-linux/usr/share/openocd/scripts'


def probe_command(board, action):
    cmd = [OCD, '-s', SCRIPTS, '-c', 'gdb_port disabled', '-c', 'tcl_port disabled', '-c', 'telnet_port disabled']
    if board == 'Reciever':
        cmd += ['-f', 'interface/stlink.cfg', '-c', 'adapter serial 0666FF485753667187162860', '-f', 'target/stm32l4x.cfg', '-c', 'reset_config srst_only srst_nogate']
    else:
        cmd += ['-f', 'interface/jlink.cfg', '-c', 'adapter serial 685278459', '-c', 'transport select swd', '-f', 'target/nrf52.cfg']
    return cmd + ['-c', 'adapter speed 4000', '-c', action]


def run(cmd, logfile):
    with logfile.open('w') as f:
        proc = subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT,
                              env={**os.environ, 'CCACHE_DISABLE': '1'}, timeout=180)
    if proc.returncode:
        raise RuntimeError(f'Command failed ({proc.returncode}); see {logfile}\n' + logfile.read_text()[-3000:])


def build(board, half, sample, duration, label, diagnostic=0, hse=False, fast_gpio=False, edge_decoder=False, edge_irq=False, hw_tx=False, zero_latency=False):
    cmd = ['cmake', '-S', str(ROOT / board), '-B', str(ROOT / board / 'build'), f'-DHALF_BIT_US={half}']
    if board == 'T_test':
        cmd += [f'-DTX_HW_WAVEFORM={int(hw_tx)}']
    if board == 'Reciever':
        cmd += [f'-DSAMPLE_US={sample}', f'-DACQUISITION_MS={duration}', f'-DDIAG_SAMPLE_COUNT={diagnostic}', f'-DRX_FAST_GPIO={int(fast_gpio)}', f'-DRX_EDGE_DECODER={int(edge_decoder)}', f'-DRX_EDGE_IRQ={int(edge_irq)}', f'-DRX_ZERO_LATENCY={int(zero_latency)}',
                '-DEXTRA_DTC_OVERLAY_FILE=' + (str(OUT / 'stm32_hse.overlay') if hse else '')]
    run(cmd, OUT / 'raw' / (label + '_' + board + '_configure.txt'))
    run(['cmake', '--build', str(ROOT / board / 'build'), '-j2'], OUT / 'raw' / (label + '_' + board + '_build.txt'))


def flash(board, label):
    image = ROOT / board / 'build/zephyr/zephyr.elf'
    if board == 'T_test':
        commands = OUT / 'raw' / (label + '_jlink_commands.txt')
        commands.write_text(f'r\nh\nloadfile {image}\nr\ng\nq\n')
        import serial
        with serial.Serial('/dev/ttyACM1', 115200, timeout=0.5) as tx_port:
            tx_port.reset_input_buffer()
            run(['JLinkExe', '-USB', '685278459', '-device', 'nRF52833_xxAA',
             '-if', 'SWD', '-speed', '4000', '-autoconnect', '1', '-ExitOnError', '1',
             '-Log', str(OUT / 'raw' / (label + '_jlink_log.txt')),
             '-CommanderScript', str(commands)], OUT / 'raw' / (label + '_' + board + '_flash.txt'))
            banner = tx_port.read(4096)
            (OUT / 'raw' / (label + '_tx.txt')).write_bytes(banner)
    else:
        run(probe_command(board, f'program {image} verify reset exit'), OUT / 'raw' / (label + '_' + board + '_flash.txt'))


def capture(label, timeout, flash_rx=False, reset=True):
    import serial
    with serial.Serial('/dev/ttyACM0', 115200, timeout=1) as port:
        port.reset_input_buffer()
        if flash_rx:
            flash('Reciever', label)
        elif reset:
            run(probe_command('Reciever', 'init; reset run; shutdown'), OUT / 'raw' / (label + '_reset.txt'))
        header = None
        deadline = time.monotonic() + timeout
        with (OUT / 'raw' / (label + '_rx.txt')).open('w') as raw:
            while time.monotonic() < deadline:
                line = port.readline().decode(errors='replace').strip()
                if not line:
                    continue
                raw.write(line + '\n')
                raw.flush()
                if line.startswith('half_bit_us,sample_us,'):
                    header = next(csv.reader([line]))
                elif header:
                    values = next(csv.reader([line]))
                    if len(values) == len(header):
                        try:
                            return dict(zip(header, map(int, values)))
                        except ValueError:
                            pass
    raise RuntimeError(f'No result within {timeout}s: {label}')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--half', type=int, default=48)
    p.add_argument('--sample', type=int, default=12)
    p.add_argument('--duration', type=int, default=10000)
    p.add_argument('--zero-latency', action='store_true')
    p.add_argument('--hw-tx', action='store_true')
    p.add_argument('--edge-irq', action='store_true')
    p.add_argument('--edge-decoder', action='store_true')
    p.add_argument('--fast-gpio', action='store_true')
    p.add_argument('--hse', action='store_true', help='Use onboard ST-Link 8 MHz MCO for STM32 PLL')
    p.add_argument('--diagnostic', type=int, default=0)
    p.add_argument('--repeat', type=int, default=1)
    p.add_argument('--label', default='sweep')
    p.add_argument('--baseline', action='store_true', help='Only reset RX and capture currently flashed firmware')
    p.add_argument('--keep-tx', action='store_true', help='Do not rebuild or flash TX; timing must match')
    args = p.parse_args()
    if args.half <= 0 or args.sample <= 0 or args.half % args.sample or args.half // args.sample < 2:
        p.error('Require positive integral oversampling >= 2')
    from serial.tools import list_ports
    actual = {port.device: port.serial_number for port in list_ports.comports()}
    for device, serial_number in {'/dev/ttyACM0': '0666FF485753667187162860', '/dev/ttyACM1': '000685278459'}.items():
        if actual.get(device) != serial_number:
            raise RuntimeError(f'Unexpected board on {device}: {actual.get(device)!r}; no flash performed')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    label = stamp + '_' + args.label
    (OUT / 'raw').mkdir(exist_ok=True)
    metadata = {'args': vars(args), 'timestamp_utc': stamp, 'sources': {},
                'flashed_source_known': not args.baseline,
                'note': 'Baseline source snapshots describe workspace, not necessarily installed firmware.' if args.baseline else ''}
    for board in ['Reciever', 'T_test']:
        source = ROOT / board / 'src/main.c'
        metadata['sources'][board] = hashlib.sha256(source.read_bytes()).hexdigest()
        (OUT / 'raw' / (label + '_' + board + '_source.c')).write_bytes(source.read_bytes())
    metadata['configuration_sources'] = {}
    for board in ['Reciever', 'T_test']:
        for relative in ['prj.conf', 'CMakeLists.txt', 'boards/' + ('nucleo_l476rg.overlay' if board == 'Reciever' else 'nrf52833dk_nrf52833.overlay')]:
            path = ROOT / board / relative
            metadata['configuration_sources'][board + '/' + relative] = hashlib.sha256(path.read_bytes()).hexdigest()
            (OUT / 'raw' / (label + '_' + board + '_' + path.name)).write_bytes(path.read_bytes())
    (OUT / 'raw' / (label + '_metadata.json')).write_text(json.dumps(metadata, indent=2))
    if not args.baseline:
        if not args.keep_tx:
            build('T_test', args.half, args.sample, args.duration, label, hw_tx=args.hw_tx)
            flash('T_test', label)
        build('Reciever', args.half, args.sample, args.duration, label, args.diagnostic, args.hse, args.fast_gpio, args.edge_decoder, args.edge_irq, zero_latency=args.zero_latency)
    if not args.baseline:
        metadata['images'] = {board: hashlib.sha256((ROOT / board / 'build/zephyr/zephyr.elf').read_bytes()).hexdigest() for board in ['Reciever', 'T_test']}
        (OUT / 'raw' / (label + '_metadata.json')).write_text(json.dumps(metadata, indent=2))
    for rep in range(args.repeat):
        name = label + f'_r{rep + 1}'
        row = capture(name, 80 if args.baseline else args.duration / 1000 + 15,
                      flash_rx=not args.baseline and rep == 0)
        row = {'timestamp_utc': stamp, 'source': 'hardware', 'label': args.label,
               'repeat': rep + 1, 'port': '/dev/ttyACM0', **row}
        with (OUT / (name + '.csv')).open('w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=list(row))
            writer.writeheader()
            writer.writerow(row)
        print(json.dumps(row), flush=True)


if __name__ == '__main__':
    main()
