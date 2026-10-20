"""Capture one RX acquisition without resetting either board."""
import argparse
import csv
from datetime import datetime, timezone
from pathlib import Path
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyACM0')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--timeout', type=float, default=90)
    args = parser.parse_args()
    import serial
    root = Path(__file__).resolve().parent
    (root / 'raw').mkdir(exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    header = None
    with serial.Serial(args.port, args.baud, timeout=0.5) as port:
        # Avoid deliberate DTR/RTS reset; start capture before manually resetting RX.
        with (root / 'raw' / (stamp + '.txt')).open('w') as raw:
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                line = port.readline().decode('utf-8', errors='replace').strip()
                if not line:
                    continue
                raw.write(line + '\n')
                raw.flush()
                print(line, flush=True)
                if line.startswith('half_bit_us,sample_us,'):
                    header = next(csv.reader([line]))
                elif header and ',' in line:
                    values = next(csv.reader([line]))
                    if len(values) != len(header):
                        continue
                    try:
                        numbers = [int(v) for v in values]
                    except ValueError:
                        continue
                    row = dict(zip(header, numbers))
                    # One file per acquisition allows old/new firmware columns to coexist.
                    row = {'timestamp_utc': stamp, 'source': 'hardware', 'port': args.port, **row}
                    with (root / (stamp + '.csv')).open('w', newline='') as out:
                        writer = csv.DictWriter(out, fieldnames=list(row))
                        writer.writeheader()
                        writer.writerow(row)
                    print('Saved:', root / (stamp + '.csv'))
                    return
    raise SystemExit('No complete CSV result received; raw capture preserved. Start capture before resetting RX.')


if __name__ == '__main__':
    main()
