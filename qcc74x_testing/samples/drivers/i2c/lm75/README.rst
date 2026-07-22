.. Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
.. SPDX-License-Identifier: BSD-3-Clause

.. zephyr:code-sample:: LM75_I2C
   :name: LM75 I2C

   Print "Temperature" to the console From LM75 By I2C.

Overview
********

This is an example of reading the current ambient temperature from an LM75 chip via an I2C channel.

Building and Running
********************

This application can be built and executed on QEMU as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/LM75_I2C
   :host-os: unix
   :board: qcc743
   :goals: run
   :compact:


Sample Output
=============

.. code-block:: console

    Now Temp: 27.50°C
