.. Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
.. SPDX-License-Identifier: BSD-3-Clause

.. zephyr:code-sample:: adc_dac_loopback
   :name: ADC DAC loopback

   Receive audio input from a microphone connected to audio ADC and loopback
   audio to speaker connected to audio DAC.

Overview
********

This sample provides an example of using audio ADC and audio DAC with DMA
transfers. It reads analog audio input into a buffer and writes the same buffer
as analog audio output. The sample has a loop for enabling and disabling
loopback to demonstrate that stopping and restarting is possible.

Non-differential mode is used by default for a microphone. Differential mode
is used for output.

Example connections for QCC744M EVK:
- Microphone https://www.sparkfun.com/sparkfun-analog-mems-microphone-breakout-sph8878lr5h-1.html
  - AUD - GPIO27 (= audio ADC positive channel, GPIO27_AUADC_CH4)
  - VCC - VDD_3V3
  - GND - GND
- Active speaker left channel connection e.g. through TRRS jack https://www.adafruit.com/product/5764
  - Left - GPIO22 (= differential positive signal, GPIO22_AUDAC_PWM_P)
  - Ring - GPIO23 (= differential negative signal, GPIO23_AUDAC_PWM_N)

Sound recorded by the microphone should be played back from the speaker with a
small delay between "Start loopback" and "Stop loopback" prints.

Sample Output
=============

 .. code-block:: console

    Start loopback
    Stop loopback
    Start loopback
    Stop loopback

    <repeats endlessly>
