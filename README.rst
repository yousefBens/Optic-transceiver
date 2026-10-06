OPV4COM - Optical Transmitter and Receiver
===========================================

This project implements and tests a simple optical communication system based
on Manchester encoding using Zephyr RTOS.

The communication is divided into two main parts:

- ``Transmitter``: development of the optical transmitter.
- ``Reciever``: development of the optical receiver.
- ``T_test``: test transmitter implemented on an nRF52833 DK to validate the
  receiver independently from the final optical transmitter hardware.

The current test setup uses the nRF52833 DK as a known digital transmitter and
the STM32L476RG Nucleo board as the receiver.

The objective is to validate the complete reception chain:

::

    nRF52833 DK
    Test Transmitter
         |
         | Manchester signal
         |
         v
    STM32L476RG
       Receiver
         |
         v
    Preamble detection
         |
         v
    Manchester decoding
         |
         v
    Original byte


1. T_test - nRF52833 Test Transmitter
======================================

The ``T_test`` application is implemented on an nRF52833 DK using Zephyr RTOS.

Its purpose is to generate a known Manchester signal in order to test and
validate the STM32L476RG receiver before using the complete optical link.

The transmitter generates the signal on a GPIO using a hardware timer.

Timing
------

The current half-bit duration is:

::

    HALF_BIT_US = 40 us

Therefore, one complete Manchester bit takes:

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

    Original bit 0  ->  01
    Original bit 1  ->  10

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

After Manchester encoding:

::

    Original:
       0    0    0    1    0    1    1    0

    Manchester:
      01   01   01   10   01   10   10   01


Preamble
--------

Before each byte, the transmitter sends an alternating preamble:

::

    01010101

Each preamble level lasts 40 us.

The alternating pattern generates regular edges separated by approximately
40 us. These edges are used by the receiver to detect the beginning of a
transmission and synchronize with the transmitter.

The transmitted frame is therefore:

::

    +------------+-----------------------------------+
    | PREAMBLE   | DATA                              |
    +------------+-----------------------------------+
    | 01010101   | 01 01 01 10 01 10 10 01         |
    +------------+-----------------------------------+

                   Manchester encoded 0x16


Transmitter State Machine
-------------------------

The transmitter uses two states:

::

                +------------+
                |  PREAMBLE  |
                +-----+------+
                      |
                      | 8 half-bits
                      v
                +------------+
                |    DATA    |
                +-----+------+
                      |
                      | 8 bits transmitted
                      v
                +------------+
                |  PREAMBLE  |
                +------------+

A hardware timer generates an event every 40 us.

During ``PREAMBLE``, the GPIO alternates between LOW and HIGH.

During ``DATA``, each original bit is converted into its corresponding
Manchester pair.

After the complete byte has been transmitted, the transmitter returns to the
``PREAMBLE`` state and starts a new frame.


2. Receiver - STM32L476RG
==========================

The receiver is implemented on an STM32L476RG Nucleo board using Zephyr RTOS.

Its objective is to detect the beginning of a frame, synchronize with the
transmitter, sample the incoming Manchester signal and reconstruct the
original byte.

The receiver operates in two main phases:

::

    WAIT_PREAMBLE
          |
          v
    Detect regular edges
          |
          v
    RECEIVE_DATA
          |
          v
    Sample Manchester signal
          |
          v
    Decode 8 bits
          |
          v
    Reconstruct original byte


Preamble Detection
------------------

Initially, the receiver is in:

::

    WAIT_PREAMBLE

The input GPIO is configured with interrupts on both rising and falling edges.

For each detected edge, the receiver reads a free-running hardware timer and
calculates the time difference between two consecutive edges:

::

    Edge        Edge        Edge        Edge
      |           |           |           |
      +-- 40 us --+-- 40 us --+-- 40 us --+

The expected interval is approximately 40 us.

A tolerance window is currently used:

::

    PREAMBLE_MIN_US = 30 us
    PREAMBLE_MAX_US = 50 us

If several consecutive edge intervals are inside this window, the receiver
recognizes the preamble.

The receiver then switches from:

::

    WAIT_PREAMBLE

to:

::

    RECEIVE_DATA


Data Sampling
-------------

Once the preamble is detected, GPIO edge interrupts are disabled.

The hardware timer is then used to periodically sample the GPIO input.

The objective is to obtain the two levels corresponding to each Manchester
bit:

::

          first_half       second_half
               |                |
               v                v
          +----------+     +----------+
          |  40 us   |     |  40 us   |
          +----------+-----+----------+
                       |
                       v
                Manchester decoder


Manchester Decoder
------------------

The receiver uses the same Manchester convention as the transmitter:

::

    01 -> original bit 0
    10 -> original bit 1

The combinations:

::

    00 -> invalid
    11 -> invalid

are considered Manchester decoding errors.

For the current test byte:

::

    Received Manchester:

    01  01  01  10  01  10  10  01

     |   |   |   |   |   |   |   |
     v   v   v   v   v   v   v   v

     0   0   0   1   0   1   1   0

The reconstructed byte is therefore:

::

    Binary  : 00010110
    HEX     : 0x16
    Decimal : 22


Receiver State Machine
----------------------

The receiver operation can be summarized as:

::

                         +----------------+
                         | WAIT_PREAMBLE  |
                         +-------+--------+
                                 |
                           GPIO edges
                                 |
                                 v
                         Measure delta t
                                 |
                                 v
                      30 us <= dt <= 50 us
                                 |
                                 v
                       Preamble detected
                                 |
                                 v
                         +---------------+
                         | RECEIVE_DATA  |
                         +-------+-------+
                                 |
                          Timer sampling
                                 |
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


3. Current Test Setup
=====================

The current test is intended to validate the receiver without using the final
optical transmitter.

The setup is:

::

    +------------------+
    |   nRF52833 DK    |
    |     T_test       |
    +--------+---------+
             |
             | GPIO
             | Manchester
             |
             v
    +------------------+
    |   STM32L476RG    |
    |     Receiver     |
    +--------+---------+
             |
             v
       Manchester
         decoder
             |
             v
          0x16


For a direct electrical test, the two boards must share a common ground:

::

    nRF52833 DK                 STM32L476RG
    ------------                ------------
    DATA GPIO  ----------------> DATA INPUT
    GND        ----------------- GND


The expected receiver output for the current test is:

::

    Frame received
    Original data : 00010110
    HEX           : 0x16
    Decimal       : 22


4. Final Objective
==================

The nRF52833 ``T_test`` application is only used to validate the digital
communication and receiver implementation.

Once the receiver has been validated, the complete system will use the optical
communication chain:

::

    Transmitter
        |
        v
    LED / Lamp
        |
        | Light
        v
    Optical Receiver
        |
        v
    Analog Front-End
        |
        v
    STM32L476RG
        |
        v
    Preamble Detection
        |
        v
    Manchester Decoder
        |
        v
    Original Data