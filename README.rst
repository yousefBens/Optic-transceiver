1. Transmitter
==============

The transmitter is implemented on an STM32L476RG using Zephyr RTOS.

The objective is to transmit binary data through a GPIO using Manchester
encoding. Each bit is divided into two equal half-bit periods:

- ``0`` -> LOW then HIGH
- ``1`` -> HIGH then LOW

The current implementation uses a half-bit duration of ``500 us``, corresponding
to a complete bit duration of ``1 ms`` and therefore a data rate of ``1 kbit/s``.

A hardware timer is used to generate an interrupt every ``500 us``. The timer
callback controls the GPIO state and manages the transmission using two states:

- ``PREAMBLE``: the output is held LOW for 6 half-bit periods before transmitting data.
- ``DATA``: the byte is transmitted bit by bit, from MSB to LSB, using Manchester encoding.

The current test byte is:

::

    data_tosend = 0x16

which corresponds to:

::

    0x16 = 0001 0110

Transmission sequence:

::

                PREAMBLE                 DATA (Manchester)
        <--------------------> <-------------------------------->
    DATA ____ ____ ____ ____    0   0   0   1   0   1   1   0
                                 ↕   ↕   ↕   ↕   ↕   ↕   ↕   ↕
                               LH  LH  LH  HL  LH  HL  HL  LH

    L = 0 V
    H = 3.3 V

After transmitting the complete byte, the state machine returns to the
``PREAMBLE`` state and the transmission starts again.