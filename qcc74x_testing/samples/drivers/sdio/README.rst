.. Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
.. SPDX-License-Identifier: BSD-3-Clause


SDIO test app
#############

Overview
********

This app shows usage of QCC74x SDIO driver.
The flow of application imitates QCC SDK example app:
https://git.codelinaro.org/clo/qcc7xx/QCCSDK-QCC74x/-/blob/master/examples/peripherals/sdio/sdio2_test/main.c
This needs the host device. It was tested with STM32 as a host.

Hardware Setup
**************

Connect the QCC74x to the host MCU according to the following pin
assignments.

====   ======  ===================
SDIO    QCC    STM32 NUCLEO-F767ZI
====   ======  ===================
D0     GPIO14  PC8
D1     GPIO15  PC9
D2     GPIO10  PC10
D3     GPIO11  PC11
CLK    GPIO13  PC12
CMD    GPIO12  PD2
GND    GND     GND
=====  =====   ===================

Expected Output
***************

::
    sdio2 wait host ready...
    sdio2 host ready!
    sdio2 bus width: 1
    sdio2 block size: 512
    sdio dnld max size 2048
    sdio upld max size 64
    dnld done, tag 0x0011, len 17, upld it!
    upld done, push buff to dnld queue
    sdio2 test, dnld_cnt: 1, upld_cnt: 1
