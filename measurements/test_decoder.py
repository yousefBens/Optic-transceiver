"""Compile and exercise the actual receiver decoder on generated Manchester samples."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
source = (root.parent / 'Reciever/src/main.c').read_text()
# Keep protocol constants and the actual state machine, excluding Zephyr I/O.
constants = source[source.index('#ifndef HALF_BIT_US'):source.index('#define DATA_NODE')]
decoder = source[source.index('static const uint8_t expected_payload'):source.index('static void __attribute__((unused)) timer_callback')]
harness = r'''
static void emit(uint8_t byte) {
    for (int bit = 7; bit >= 0; --bit) {
        uint8_t v = (byte >> bit) & 1;
        for (unsigned i = 0; i < SAMPLES_PER_HALF; ++i) process_sample(v);
        for (unsigned i = 0; i < SAMPLES_PER_HALF; ++i) process_sample(!v);
    }
}
static void gap(void) {
    for (unsigned i = 0; i < INTER_FRAME_HALFS * SAMPLES_PER_HALF; ++i) process_sample(1);
}
int main(void) {
    for (unsigned n = 0; n < 1000; ++n) {
        gap(); emit(PREAMBLE); emit(SFD); emit(0xFF); emit(0x16); emit(0xC1);
    }
    assert(frame_ok == 1000 && frame_bad == 0 && manchester_errors == 0);
    gap(); emit(PREAMBLE); emit(SFD); emit(0xFE); emit(0x16); emit(0xC1);
    assert(frame_bad == 1 && payload_bit_errors == 1);
    gap(); emit(0x54);
    assert(preamble_errors == 1);
    gap(); emit(PREAMBLE); emit(0xD2);
    assert(sfd_errors == 1);
    gap(); process_sample(0);
    for (unsigned i = 1; i < 3 * SAMPLES_PER_HALF; ++i) process_sample(0);
    assert(manchester_errors == 1);
    gap(); emit(PREAMBLE); emit(SFD); emit(0xFF); emit(0x16); emit(0xC1);
    assert(frame_ok == 1001);
#if RX_EDGE_DECODER
    uint8_t wave[FRAME_HALFS];
    unsigned wi = 0;
    for (unsigned i = 0; i < INTER_FRAME_HALFS; i++) wave[wi++] = 1;
    const uint8_t bytes[] = {PREAMBLE, SFD, 0xFF, 0x16, 0xC1};
    for (unsigned j = 0; j < sizeof(bytes); j++) {
        for (int bit = 7; bit >= 0; bit--) {
            unsigned v = (bytes[j] >> bit) & 1;
            wave[wi++] = v; wave[wi++] = !v;
        }
    }
    for (unsigned rate = 985; rate <= 1015; rate += 30) {
        reset_decoder();
        frame_ok = frame_bad = frame_count = manchester_errors = 0;
        preamble_errors = sfd_errors = 0;
        unsigned total = FRAME_HALFS * SAMPLES_PER_HALF * rate;
        for (unsigned i = 0; i < total; i++) {
            unsigned half = ((uint64_t)i * 1000U) / (SAMPLES_PER_HALF * rate);
            process_sample(wave[half % FRAME_HALFS]);
        }
        assert(frame_ok >= 999 && frame_bad == 0 && manchester_errors == 0);
    }
    puts("PASS: 1000 frames with +1.5% and -1.5% sampling-clock mismatch");
#endif
#if RX_EDGE_IRQ
    reset_decoder(); rx_half_ticks = 1280; previous_edge_level = 1;
    manchester_errors = 0;
    process_edge(0, 4 * rx_half_ticks);
    assert(state == RX_PREAMBLE);
    process_edge(1, 100); /* Invalid pulse shorter than half a half-bit. */
    assert(manchester_errors == 1 && state == RX_SEARCH_GAP);
    process_edge(0, 4 * rx_half_ticks);
    process_edge(1, 3 * rx_half_ticks); /* Invalid three-half run inside a frame. */
    assert(manchester_errors == 2 && state == RX_SEARCH_GAP);
    process_edge(0, 4 * rx_half_ticks);
    process_edge(0, rx_half_ticks); /* Same level means an edge was missed. */
    assert(manchester_errors == 3 && state == RX_SEARCH_GAP);
    process_edge(0, rx_half_ticks); /* A pending startup IRQ is ignored in search. */
    assert(manchester_errors == 3);
    uint8_t edge_wave[FRAME_HALFS];
    for (unsigned last = 0xC0; last <= 0xC1; last++) {
        unsigned wi2 = 0;
        for (unsigned i = 0; i < INTER_FRAME_HALFS; i++) edge_wave[wi2++] = 1;
        const uint8_t edge_bytes[] = {PREAMBLE, SFD, 0xFF, 0x16, last};
        for (unsigned j = 0; j < sizeof(edge_bytes); j++) {
            for (int bit = 7; bit >= 0; bit--) {
                unsigned v = (edge_bytes[j] >> bit) & 1;
                edge_wave[wi2++] = v; edge_wave[wi2++] = !v;
            }
        }
        reset_decoder(); frame_ok = frame_bad = frame_count = manchester_errors = 0;
        payload_bit_errors = 0; rx_half_ticks = 1280; previous_edge_level = 1;
        unsigned run = 0; uint8_t prev = 1;
        for (unsigned i = 0; i < FRAME_HALFS * 1001; i++) {
            uint8_t level = edge_wave[i % FRAME_HALFS];
            if (level != prev) {
                process_edge(level, run * 1280 + (i % 3) * 40);
                run = 0; prev = level;
            }
            run++;
        }
        process_edge(!prev, run * 1280);
        assert(manchester_errors == 0);
        if (last == 0xC1) assert(frame_ok == 1001 && frame_bad == 0);
        else assert(frame_bad >= 1000 && payload_bit_errors == frame_bad);
    }
    puts("PASS: timestamped GPIO edges, jitter, final HIGH merging with gap");
#endif
    puts("PASS: 1000 ideal frames, bit error, preamble, SFD, Manchester error and resynchronization");
}
'''
with tempfile.TemporaryDirectory(dir=root) as tmp:
    c = Path(tmp) / 'decoder.c'
    c.write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <assert.h>\n#include <stdio.h>\n' + constants + decoder + harness)
    binary = Path(tmp) / 'decoder'
    for edge, irq in [(0, 0), (1, 0), (0, 1)]:
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-DHALF_BIT_US=48', '-DSAMPLE_US=12', f'-DRX_EDGE_DECODER={edge}', f'-DRX_EDGE_IRQ={irq}', str(c), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
