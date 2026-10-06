OPV4COM - Optical Transmitter and Receiver
==========================================

This project implements and tests an optical communication system based
on Manchester encoding using Zephyr RTOS.

The communication is divided into three development parts:

- ``Transmitter``: final optical transmitter development.
- ``Reciever``: STM32L476RG receiver development.
- ``T_test``: digital test transmitter implemented on an nRF52833 DK to
  validate the receiver before using the complete optical hardware.

The current test setup is:

::

    nRF52833 DK
         |
         | T_test
         | PREAMBLE + SYNC + Manchester DATA
         |
         v
    STM32L476RG
         |
         | Preamble detection
         | Synchronization detection
         | Manchester sampling
         | Manchester decoding
         |
         v
    Original byte

The current objective is to validate the digital communication and
synchronization before replacing the direct GPIO connection by the
optical transmission chain.


1. Communication Principle
===========================

The current communication frame contains three successive parts:

::

    +------------+--------+-----------------------------------+
    | PREAMBLE   | SYNC   | DATA                              |
    +------------+--------+-----------------------------------+
    | 01010101   |   1    | 01 01 01 10 01 10 10 01         |
    +------------+--------+-----------------------------------+

The half-bit duration is:

::

    HALF_BIT_US = 40 us

One complete Manchester bit therefore takes:

::

    Tbit = 2 x 40 us = 80 us

The corresponding useful data rate is:

::

    Data rate = 1 / 80 us = 12.5 kbit/s


Manchester Encoding
-------------------

Each original data bit is represented by two half-bits.

The convention used in this project is:

::

    Original bit 0 -> 01
    Original bit 1 -> 10

Therefore:

::

    0 : LOW  -> HIGH
    1 : HIGH -> LOW


Test Data
---------

The current transmitted byte is:

::

    data_tosend = 0x16

In binary:

::

    0x16 = 00010110

The original bits are:

::

    0    0    0    1    0    1    1    0

After Manchester encoding:

::

    01   01   01   10   01   10   10   01


2. T_test - nRF52833 Test Transmitter
=====================================

The ``T_test`` application is implemented on an nRF52833 DK using
Zephyr RTOS.

Its purpose is to generate a known digital Manchester signal in order
to test and validate the STM32L476RG receiver before using the complete
optical transmission chain.

The transmitter generates the signal on a GPIO using a hardware timer.

The hardware timer generates an event every:

::

    40 us

The transmitter uses three states:

::

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
                    | synchronization
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


Preamble
--------

Before each byte, the transmitter sends the following alternating
preamble:

::

    01010101

Each level lasts 40 us.

The alternating pattern generates regular rising and falling edges:

::

    0       1       0       1       0       1
    |       |       |       |       |       |
    +-------+-------+-------+-------+-------+
      40 us   40 us   40 us   40 us   40 us

These regular transitions allow the receiver to detect the presence
of a transmission and determine the transmitter timing.


Synchronization
---------------

Detecting only the preamble is not sufficient to determine the exact
beginning of DATA.

Manchester encoded DATA can also generate transitions separated by
40 us.

For example, several original zero bits produce:

::

    Original:

    0    0    0

    Manchester:

    01   01   01

Therefore, part of the Manchester DATA can look similar to the
alternating preamble.

A synchronization section is consequently inserted between PREAMBLE
and DATA.

The transmitter state machine becomes:

::

    PREAMBLE
        |
        v
       SYNC
        |
        v
       DATA

The preamble ends at HIGH:

::

    PREAMBLE

    0 1 0 1 0 1 0 1
                  |
                  v
                 HIGH

The SYNC state keeps the output HIGH for one additional 40 us period.

For the current test byte ``0x16``, the first original DATA bit is:

::

    0

Its Manchester representation is:

::

    01

Therefore, the first DATA half-bit is LOW.

The transition between synchronization and DATA is consequently a
falling edge.

The resulting timing is:

::

    Last preamble edge
            |
            v
            +---------------- HIGH
            |                 |
            |<---- 80 us ---->|
                              |
                              v
                         Falling edge
                              |
                              v
                         DATA starts

The receiver uses this approximately 80 us interval to identify the
exact beginning of DATA.


Data Transmission
-----------------

After synchronization, the transmitter sends the eight original bits
from the most significant bit to the least significant bit.

For an original zero:

::

    Original bit = 0

    first half  = 0
    second half = 1

For an original one:

::

    Original bit = 1

    first half  = 1
    second half = 0

For the current byte:

::

    0x16 = 00010110

the Manchester sequence is:

::

    01 01 01 10 01 10 10 01

After all eight bits have been transmitted, the transmitter returns
to the PREAMBLE state and starts a new frame.


3. Receiver - STM32L476RG
=========================

The receiver is implemented on an STM32L476RG Nucleo board using
Zephyr RTOS.

Its objective is to:

- detect the preamble,
- detect the synchronization marker,
- determine the exact beginning of DATA,
- sample the Manchester signal,
- decode each Manchester pair,
- reconstruct the original byte.

The receiver uses three main states:

::

    +----------------+
    | WAIT_PREAMBLE  |
    +-------+--------+
            |
            v
    +----------------+
    |   WAIT_SYNC    |
    +-------+--------+
            |
            v
    +----------------+
    | RECEIVE_DATA   |
    +-------+--------+
            |
            v
    +----------------+
    | WAIT_PREAMBLE  |
    +----------------+


4. Preamble Detection
=====================

Initially, the receiver is in:

::

    WAIT_PREAMBLE

The input GPIO is configured with interrupts on both rising and
falling edges.

Each time an edge is detected, the receiver reads the value of a
free-running hardware timer.

For two consecutive edges:

::

    Edge 1                         Edge 2
      |                              |
      v                              v
    --+------------------------------+--
                delta_t

The receiver calculates:

::

    delta_t = Edge_2_time - Edge_1_time

During the preamble, the expected interval is approximately:

::

    40 us

A tolerance window is used:

::

    PREAMBLE_MIN_US = 30 us
    PREAMBLE_MAX_US = 50 us

Therefore:

::

    30 us <= delta_t <= 50 us

is considered a valid preamble interval.

Several consecutive valid intervals are required before the receiver
accepts the preamble.

Once the preamble has been detected, the receiver changes from:

::

    WAIT_PREAMBLE

to:

::

    WAIT_SYNC


5. Synchronization Detection
============================

In the ``WAIT_SYNC`` state, the receiver continues measuring the time
between GPIO edges.

The synchronization interval is expected to be approximately:

::

    80 us

The current tolerance is:

::

    SYNC_MIN_US = 70 us
    SYNC_MAX_US = 90 us

Therefore, synchronization is detected when:

::

    70 us <= delta_t <= 90 us

The expected sequence is:

::

    Preamble edges
         |
         | 40 us
         v
    -----+------------------------- HIGH
         |                         |
         |<------- 80 us --------->|
                                   |
                                   v
                              Falling edge
                                   |
                                   v
                              DATA starts

When this falling edge is detected, the receiver knows the beginning
of the Manchester DATA.

The receiver then changes to:

::

    RECEIVE_DATA


Why SYNC Is Necessary
---------------------

Without a synchronization marker, the receiver can detect a sequence
of 40 us transitions inside the Manchester DATA itself.

This can cause the receiver to start decoding at the wrong position.

During previous tests, the expected value was:

::

    00010110 = 0x16

but the receiver sometimes obtained:

::

    00101100 = 0x2C

The two values are related by a one-bit shift:

::

    00010110
     |
     v
    00101100

or:

::

    0x16 << 1 = 0x2C

This showed that the electrical communication was working, but the
beginning of DATA was not always detected at the correct position.

The separate SYNC section provides a more precise reference for the
start of DATA.


6. Data Sampling
================

Once the synchronization edge has been detected, the receiver knows
the beginning of the first Manchester half-bit.

The receiver must not sample directly on this transition.

Instead, it waits:

::

    SAMPLE_OFFSET_US = 20 us

Because one half-bit lasts 40 us, waiting 20 us places the sample
approximately in the center of the half-bit.

::

    0 us                 20 us                 40 us
     |---------------------|---------------------|
    start                sample                  end
                           ^
                           |
                      center of
                       half-bit

The first sample is therefore taken 20 us after DATA starts.

The following samples are taken every:

::

    40 us

The sampling sequence is:

::

    DATA start
        |
        |---- 20 us ----*
                        Sample 1
                           |
                           |---- 40 us ----*
                                           Sample 2
                                              |
                                              |---- 40 us ----*
                                                              Sample 3

The receiver therefore obtains one sample for every Manchester
half-bit.


7. Manchester Decoding
======================

Two consecutive samples represent one Manchester symbol.

The receiver uses the same convention as the transmitter:

::

    first_half   second_half       Original bit

        0             1                 0

        1             0                 1

Therefore:

::

    01 -> 0
    10 -> 1

The combinations:

::

    00
    11

are invalid Manchester symbols.

If ``00`` or ``11`` is detected, the current frame is rejected.

For the current test byte:

::

    Manchester:

    01  01  01  10  01  10  10  01

     |   |   |   |   |   |   |   |
     v   v   v   v   v   v   v   v

     0   0   0   1   0   1   1   0

The reconstructed binary byte is:

::

    00010110

which corresponds to:

::

    HEX     : 0x16
    Decimal : 22


8. Complete Receiver Operation
==============================

The complete receiver operation can be represented as:

::

                        +----------------+
                        | WAIT_PREAMBLE  |
                        +-------+--------+
                                |
                                | GPIO edges
                                v
                         Measure delta_t
                                |
                                v
                     30 us <= dt <= 50 us
                                |
                                v
                    Consecutive valid edges
                                |
                                v
                        +---------------+
                        |   WAIT_SYNC   |
                        +-------+-------+
                                |
                                | GPIO edge
                                v
                         Measure delta_t
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
                                | 20 us
                                v
                           First sample
                                |
                                | every 40 us
                                v
                     +---------------------+
                     | first / second half |
                     +----------+----------+
                                |
                                v
                       Manchester decoding
                                |
                    +-----------+-----------+
                    |                       |
                  01 -> 0                 10 -> 1
                    |                       |
                    +-----------+-----------+
                                |
                                v
                         Store decoded bit
                                |
                                v
                         8 bits received
                                |
                                v
                      Reconstruct the byte
                                |
                                v
                         Frame completed
                                |
                                v
                        WAIT_PREAMBLE


9. Role of GPIO Interrupts and Hardware Timer
=============================================

The GPIO interrupt and the hardware timer have different roles.

During preamble and synchronization detection, GPIO interrupts are
used to detect signal transitions.

The hardware timer provides the timestamp associated with these
transitions.

::

    GPIO edge
        |
        v
    GPIO interrupt
        |
        v
    Read timer
        |
        v
    Calculate delta_t
        |
        +---------------------+
        |                     |
        v                     v
    approximately          approximately
       40 us                  80 us
        |                     |
        v                     v
     PREAMBLE                SYNC

During DATA reception, the hardware timer is used to generate the
sampling instants.

::

    SYNC detected
         |
         v
      wait 20 us
         |
         v
      sample GPIO
         |
         v
      wait 40 us
         |
         v
      sample GPIO
         |
         v
         ...

This avoids using software delays for the communication timing.


10. Current Test Setup
======================

The current test validates the digital communication without using the
final optical hardware.

The setup is:

::

    +------------------+
    |   nRF52833 DK    |
    |      T_test      |
    +--------+---------+
             |
             | GPIO
             |
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

For a direct electrical test, the two boards must share a common
ground:

::

    nRF52833 DK                     STM32L476RG
    ------------                    ------------

    DATA GPIO  -------------------> DATA INPUT

    GND        -------------------- GND

The nRF52833 generates the complete test frame and the STM32 receives
the digital GPIO signal directly.


Expected Result
---------------

For the current test, the transmitted original byte is:

::

    0x16

The expected receiver output is:

::

    ------------------------
    Frame received
    Original data : 00010110
    HEX           : 0x16
    Decimal       : 22
    ------------------------


11. Complete Current Communication Chain
========================================

The complete current test can be summarized as:

::

    nRF52833 hardware timer
              |
              | event every 40 us
              v
          PREAMBLE
          01010101
              |
              v
             SYNC
              |
              | approximately 80 us
              | between relevant edges
              v
        DATA start edge
              |
              v
           wait 20 us
              |
              v
          Sample GPIO
              |
              | every 40 us
              v
    01 01 01 10 01 10 10 01
              |
              v
       Manchester decoder
              |
              v
       0 0 0 1 0 1 1 0
              |
              v
          00010110
              |
              v
            0x16


12. Final Optical Objective
===========================

The nRF52833 ``T_test`` application is only used to validate the
digital communication and receiver implementation.

The current direct connection is:

::

    nRF52833
        |
        | GPIO
        v
    STM32L476RG

Once the digital communication has been validated, the final system
will replace the direct GPIO connection with the optical communication
chain.

The final architecture is:

::

    STM32 Transmitter
           |
           v
    Manchester Signal
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
      Digital Signal
           |
           v
    STM32L476RG
       Receiver
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

The purpose of the current nRF52833-to-STM32 test is therefore to
isolate and validate the digital communication protocol before adding
the optical hardware.

The development is performed progressively:

::

    Step 1
    Digital TX -> Digital RX
          |
          v
    Validate timing and Manchester decoding

    Step 2
    Add synchronization
          |
          v
    Validate reliable frame detection

    Step 3
    Replace direct GPIO connection
          |
          v
    Add optical transmitter and receiver

    Step 4
    Complete OPV4COM optical communication