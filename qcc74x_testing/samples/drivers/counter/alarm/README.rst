.. Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
.. SPDX-License-Identifier: BSD-3-Clause

.. zephyr:code-sample:: alarm_qcc74x
   :name: Counter Alarm (QCC74x project-owned copy)
   :relevant-api: counter_interface

   Implement an alarm application using the counter API, aligned with
   upstream zephyr/samples/drivers/counter/alarm's sample.drivers.counter.alarm
   scenario (default XTAL timer0 clock source, no external wiring needed).

Overview
********
Project-owned port of upstream Zephyr's counter alarm sample for boards
where ``zephyr/samples/drivers/counter/alarm/sample.yaml`` does not list a
qcc74x platform in ``platform_allow`` (``zephyr/`` stays zero-edit per
project constraints). Uses the same ``&timer0 { status = "okay"; };``
overlay and default XTAL clock source as the reference build's passing log
-- no GPIO/PWM loopback wiring required.

Sample Output
=============

 .. code-block:: console

    Counter alarm sample

    Set alarm in 2 sec (1000000 ticks)
    !!! Alarm !!!
    Now: 2
    Set alarm in 4 sec (2000000 ticks)
    !!! Alarm !!!
