.. Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
.. SPDX-License-Identifier: BSD-3-Clause

.. zephyr:code-sample:: alarm_gpio_clk_source
   :name: Counter Alarm with GPIO clock source
   :relevant-api: counter_interface

   Implement an alarm application using the counter API and GPIO input clock.

Overview
********
This sample provides an example of alarm application using :ref:`counter API <counter_api>`.
It sets an alarm with an initial delay of 2 seconds. At each alarm
expiry, a new alarm is configured with a delay multiplied by 2.
Addition: clock is taken from GPIO input pin.

.. note::
   In case of 1Hz frequency (RTC for example), precision is 1 second.
   Therefore, the sample output may differ in 1 second

Sample Output
=============

 .. code-block:: console

    Counter alarm sample

    Set alarm in 2 sec
    !!! Alarm !!!
    Now: 2
    Set alarm in 4 sec
    !!! Alarm !!!
    Now: 6
    Set alarm in 8 sec
    !!! Alarm !!!
    Now: 14
    Set alarm in 16 sec
    !!! Alarm !!!
    Now: 30

    <repeats endlessly>
