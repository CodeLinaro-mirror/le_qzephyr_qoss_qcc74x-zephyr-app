.. Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
.. SPDX-License-Identifier: BSD-3-Clause

.. zephyr:code-sample:: watchdog
   :name: Watchdog GPIO clock source
   :relevant-api: watchdog_interface

   Use the watchdog driver API to reset the board when it gets stuck in an infinite loop,
   with use of GPIO clock input.

Overview
********

This sample demonstrates how to use the watchdog driver API with GPIO clock input.

A typical use case for a watchdog is that the board is restarted in case some piece of code
is kept in an infinite loop.

Sample output
=============

You should get a similar output as below:

.. code-block:: console

	Watchdog sample application
	Attempting to test pre-reset callback
	Feeding watchdog 5 times
	Feeding watchdog...
	Feeding watchdog...
	Feeding watchdog...
	Feeding watchdog...
	Feeding watchdog...
	Waiting for reset...
	Handled things..ready to reset

.. note:: After the last message, the board will reset and the sequence will start again
