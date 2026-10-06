# OPV4COM - Optical Transmitter and Receiver

This project implements and tests an optical communication system based
on Manchester encoding using Zephyr RTOS.

The communication is divided into three development parts:

-   `Transmitter`: final optical transmitter development.
-   `Reciever`: STM32L476RG receiver development.
-   `T_test`: digital test transmitter implemented on an nRF52833 DK to
    validate the receiver before using the complete optical hardware.

The current test setup is:

``` text
nRF52833 DK
    |
    | T_test
    | PREAMBLE + SYNC + Manchester DATA
    v
STM32L476RG
    |
    | Preamble detection
    | Synchronization detection
    | Manchester sampling
    | Manchester decoding
    v
Original byte
```

The current objective is to validate the digital communication and
synchronization before replacing the direct GPIO connection by the
optical transmission chain.

# 1. Communication Principle

The current frame contains three successive parts:

``` text
+------------+------+-----------------------------------+
| PREAMBLE   | SYNC | DATA                              |
+------------+------+-----------------------------------+
| 01010101   |  1   | 01 01 01 10 01 10 10 01         |
+------------+------+-----------------------------------+
```

The half-bit duration is:

``` text
HALF_BIT_US = 40 us
```

One original Manchester bit therefore takes:

``` text
Tbit = 2 x 40 us = 80 us
```

The useful data rate is:

``` text
Data rate = 1 / 80 us = 12.5 kbit/s
```

The protocol uses the Manchester convention:

``` text
Original bit 0 -> 01
Original bit 1 -> 10
```

Therefore:

``` text
0 : LOW  -> HIGH
1 : HIGH -> LOW
```

The current test byte is:

``` text
data_tosend = 0x16
```

In binary:

``` text
0x16 = 00010110
```

After Manchester encoding:

``` text
Original:
   0    0    0    1    0    1    1    0

Manchester:
  01   01   01   10   01   10   10   01
```

# 2. T_test - nRF52833 Test Transmitter

The `T_test` application runs on an nRF52833 DK with Zephyr RTOS.

Its purpose is to generate a known digital signal so that the STM32
receiver can be developed and validated independently from the LED
driver, optical channel and analog receiver.

The transmitter uses a GPIO for the communication signal and a hardware
timer to control the timing.

The timer generates an event every:

``` text
40 us
```

The transmitter state machine contains three states:

``` text
              +------------+
              |  PREAMBLE  |
              +-----+------+
                    |
                    | 8 x 40 us
                    v
              +------------+
              |    SYNC    |
              +-----+------+
                    |
                    | 1 x 40 us
                    v
              +------------+
              |    DATA    |
              +-----+------+
                    |
                    | 8 Manchester bits
                    v
              +------------+
              |  PREAMBLE  |
              +------------+
```

## 2.1 PREAMBLE

Before each byte, the transmitter sends:

``` text
01010101
```

Each level lasts 40 us.

This alternating sequence generates regular rising and falling edges.
The receiver measures the interval between consecutive edges and expects
approximately:

``` text
40 us
```

The preamble is therefore used to detect that a new transmission is
arriving and to establish the timing reference.

## 2.2 SYNC

Detecting only the alternating preamble is not sufficient.

Manchester DATA can also contain transitions separated by 40 us. For
example, consecutive original zeros produce:

``` text
0 -> 01
0 -> 01
0 -> 01

Manchester:
01 01 01
```

This can look similar to part of the preamble.

For this reason, a `SYNC` state is inserted between the preamble and the
DATA.

The preamble finishes at HIGH:

``` text
01010101
       ^
       HIGH
```

The SYNC keeps the GPIO HIGH for one additional 40 us period:

``` text
PREAMBLE          SYNC
... 0 | 1 |         1
        <---- 80 us ---->
```

The 80 us value is the time between the last preamble transition and the
next transition at the beginning of DATA for the current test byte
`0x16`.

The first original bit of `0x16` is zero. Its Manchester representation
starts with LOW:

``` text
0 -> 01
```

Therefore the beginning of DATA creates a falling edge:

``` text
                    DATA
                     0
                     |
HIGH ----------------+
                     |
                     +------ LOW
```

The receiver detects this approximately 80 us interval as the
synchronization marker.

## 2.3 DATA

After synchronization, the transmitter sends the eight original bits
from MSB to LSB.

For each original bit:

``` text
bit = 0

first half  = 0
second half = 1
```

and:

``` text
bit = 1

first half  = 1
second half = 0
```

For `0x16`:

``` text
00010110
```

the transmitted Manchester DATA is:

``` text
01 01 01 10 01 10 10 01
```

After the last bit, the transmitter returns to `PREAMBLE` and starts
another frame.

# 3. Receiver - STM32L476RG

The receiver runs on the STM32L476RG Nucleo board using Zephyr RTOS.

Its role is to:

``` text
Detect PREAMBLE
       |
       v
Detect SYNC
       |
       v
Determine DATA start
       |
       v
Sample every 40 us
       |
       v
Group samples by two
       |
       v
Manchester decoding
       |
       v
Reconstruct 8-bit byte
```

The receiver uses three states:

``` text
WAIT_PREAMBLE
      |
      v
WAIT_SYNC
      |
      v
RECEIVE_DATA
      |
      v
WAIT_PREAMBLE
```

# 4. WAIT_PREAMBLE

Initially:

``` text
state = WAIT_PREAMBLE
```

The input GPIO is configured as an input with interrupts on both rising
and falling edges.

For every edge, the receiver reads the free-running hardware timer.

If two edges are detected at:

``` text
t1
t2
```

the receiver calculates:

``` text
delta_t = t2 - t1
```

During the preamble:

``` text
Edge        Edge        Edge        Edge
  |           |           |           |
  +--40 us----+--40 us----+--40 us----+
```

A tolerance is used because the measured value does not have to be
exactly 40 us:

``` text
PREAMBLE_MIN_US = 30 us
PREAMBLE_MAX_US = 50 us
```

If several consecutive intervals are inside this range, the receiver
considers that the preamble has been detected.

It then changes state:

``` text
WAIT_PREAMBLE
      |
      v
WAIT_SYNC
```

# 5. WAIT_SYNC

After the preamble has been recognized, the receiver does not
immediately start decoding DATA.

It waits for the synchronization interval.

The expected interval is approximately:

``` text
80 us
```

with the current tolerance:

``` text
SYNC_MIN_US = 70 us
SYNC_MAX_US = 90 us
```

The expected signal is:

``` text
last preamble edge
        |
        v
--------+---------------- HIGH
        |<---- 80 us ---->|
                         falling edge
                              |
                              v
                         DATA starts
```

When this interval is detected:

``` text
70 us <= delta_t <= 90 us
```

the receiver knows the position of the beginning of DATA and changes to:

``` text
RECEIVE_DATA
```

This synchronization stage was added because using the preamble alone
can produce an ambiguous start position and cause shifted bytes such as:

``` text
Expected:
00010110 = 0x16

Shifted:
00101100 = 0x2C
```

`0x2C` corresponds to `0x16` shifted by one bit to the left:

``` text
0x16 << 1 = 0x2C
```

The separate synchronization marker is intended to remove this
ambiguity.

# 6. RECEIVE_DATA

When the synchronization edge is detected, this edge corresponds to the
beginning of the first Manchester half-bit.

Sampling directly on an edge is undesirable because the signal is
changing at that instant.

The receiver therefore waits half of a half-bit:

``` text
SAMPLE_OFFSET_US = 20 us
```

The first sample is placed at the center of the first 40 us half-bit:

``` text
DATA start
    |
    v
----+----------------
    |      *
    |      |
    |     20 us
    |
    +---- first sample
```

After the first sample, the receiver samples periodically every:

``` text
40 us
```

The sequence is therefore:

``` text
DATA start
    |
    |----20 us----*
                  sample 1
                   |
                   |----40 us----*
                                 sample 2
                                  |
                                  |----40 us----*
                                                sample 3
```

Two consecutive samples form one Manchester bit:

``` text
sample 1 + sample 2
        |
        v
Manchester pair
```

# 7. Manchester Decoder

The receiver uses the same convention as the transmitter:

``` text
01 -> original bit 0
10 -> original bit 1
```

The combinations:

``` text
00
11
```

are invalid Manchester symbols.

If one of these combinations is detected, the frame is rejected.

For the current test:

``` text
Received Manchester:

01  01  01  10  01  10  10  01

 |   |   |   |   |   |   |   |
 v   v   v   v   v   v   v   v

 0   0   0   1   0   1   1   0
```

The reconstructed byte is:

``` text
Binary  : 00010110
HEX     : 0x16
Decimal : 22
```

# 8. Complete Receiver State Machine

The complete reception sequence is:

``` text
                    +----------------+
                    | WAIT_PREAMBLE  |
                    +-------+--------+
                            |
                      GPIO edges
                            |
                            v
                    Measure delta_t
                            |
                            v
                 30 us <= dt <= 50 us
                            |
                            v
                 consecutive intervals
                            |
                            v
                    +---------------+
                    |   WAIT_SYNC   |
                    +-------+-------+
                            |
                     next GPIO edge
                            |
                            v
                 70 us <= dt <= 90 us
                            |
                            v
                     DATA start found
                            |
                            v
                    +---------------+
                    | RECEIVE_DATA  |
                    +-------+-------+
                            |
                        wait 20 us
                            |
                            v
                      first sample
                            |
                            v
                   sample every 40 us
                            |
                            v
                  first / second half
                            |
                            v
                  Manchester decoding
                            |
                 +----------+----------+
                 |                     |
              01 -> 0               10 -> 1
                 |                     |
                 +----------+----------+
                            |
                            v
                    store decoded bit
                            |
                            v
                     8 bits received
                            |
                            v
                   reconstruct byte
                            |
                            v
                     frame completed
                            |
                            v
                    WAIT_PREAMBLE
```

# 9. Current Electrical Test Setup

The current test validates the digital transmitter and receiver without
the optical hardware.

``` text
+------------------+
|   nRF52833 DK    |
|      T_test      |
+--------+---------+
         |
         | GPIO
         | PREAMBLE
         | SYNC
         | Manchester DATA
         |
         v
+------------------+
|   STM32L476RG    |
|     Receiver     |
+--------+---------+
         |
         v
   Preamble detection
         |
         v
    Sync detection
         |
         v
  Manchester sampling
         |
         v
  Manchester decoding
         |
         v
       0x16
```

For the direct electrical test, the two boards must share a common
ground:

``` text
nRF52833 DK                 STM32L476RG
------------                ------------

DATA GPIO  ----------------> DATA INPUT

GND        ----------------- GND
```

The expected output is:

``` text
Frame received
Original data : 00010110
HEX           : 0x16
Decimal       : 22
```

# 10. Why a Hardware Timer is Used

The communication timing is short:

``` text
Half-bit = 40 us
```

The transmitter therefore uses a hardware timer instead of software
delays.

This provides a stable timing reference for each transmitted half-bit.

The receiver also uses a hardware timer for two different operations.

During `WAIT_PREAMBLE` and `WAIT_SYNC`, it timestamps GPIO edges.

During `RECEIVE_DATA`, it schedules the sampling instants.

The GPIO interrupt and hardware timer therefore have different roles:

``` text
GPIO interrupt
      |
      +---- detect signal transitions
      |
      +---- measure PREAMBLE timing
      |
      +---- detect SYNC

Hardware timer
      |
      +---- provide timestamp
      |
      +---- schedule Manchester samples
```

# 11. Why Sampling is Done at the Center

The receiver does not sample exactly at a transition.

For a 40 us half-bit:

``` text
0 us                20 us                40 us
 |--------------------|--------------------|
start               center                end
                      ^
                      |
                   sample
```

Sampling at approximately 20 us places the reading far from the expected
transition boundaries.

This gives more timing margin for interrupt latency, clock differences
and signal propagation delay.

# 12. Current Test Sequence

The complete current test can be represented as:

``` text
nRF timer
    |
    v
40 us event
    |
    v
PREAMBLE
01010101
    |
    v
SYNC
HIGH maintained
    |
    v
80 us edge interval detected by STM32
    |
    v
DATA start
    |
    v
20 us
    |
    v
first sample
    |
    v
samples every 40 us
    |
    v
01 01 01 10 01 10 10 01
    |
    v
0  0  0  1  0  1  1  0
    |
    v
00010110
    |
    v
0x16
```

# 13. Final Optical Objective

`T_test` is only a digital validation tool.

After the digital protocol and receiver have been validated, the direct
GPIO link will be replaced by the optical communication chain:

``` text
STM32 Transmitter
       |
       v
Manchester DATA
       |
       v
LED Driver
       |
       v
LED / Lamp
       |
       | Light
       v
Photodetector
       |
       v
Analog Front-End
       |
       v
Digital signal
       |
       v
STM32L476RG Receiver
       |
       v
Preamble Detection
       |
       v
Synchronization
       |
       v
Manchester Sampling
       |
       v
Manchester Decoder
       |
       v
Original Data
```

The current nRF52833-to-STM32 test isolates the digital protocol from
the optical hardware. This makes it possible to validate timing,
synchronization and Manchester decoding before adding the LED driver,
optical channel and analog front-end.