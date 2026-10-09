#!/usr/bin/env python3
# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause
#
# qconn_pre_flash.py - Pre-flash hook for QCC74x boards.
#
# Sends "platform reboot --edl\r\n" over the serial port to put the board
# into EDL (Emergency Download) mode before the flash tool connects.
#
# Works with both firmware variants:
#   - CONFIG_SHELL=y          (shell_stub.conf)   – shell parses the command
#   - CONFIG_QCC74X_UART_EDL_TRIGGER=y (uart_edl_stub.conf) – lightweight
#     UART listener matches the same string without a shell
#
# The host script is identical in both cases; no --magic flag needed.
#
# Usage (called by twister via pre_script in ci-hardware-map-win.yaml):
#   python qconn_pre_flash.py <serial_port> [baud]
#
# Always exits 0: the board may already be in download mode from a
# previous failed flash attempt, which is fine.

import sys
import os
import time

# NOTE: Do NOT call sys.stdout.reconfigure() here.
# When stdout is captured via subprocess.PIPE (as twister does),
# reconfigure() can break the pipe on Windows, causing all output to be lost.
# All output in this script is ASCII-safe so encoding doesn't matter.

def log(msg):
    """Print with immediate flush so output reaches twister's PIPE capture."""
    line = f"[pre_flash] {msg}"
    sys.stdout.write(line + "\n")
    sys.stdout.flush()

try:
    import serial
except ImportError:
    log("ERROR: pyserial not installed! pip install pyserial")
    sys.exit(0)

EDL_CMD = b"platform reboot --edl\r\n"

if len(sys.argv) < 2:
    log("Usage: qconn_pre_flash.py <serial_port> [baud]")
    sys.exit(1)

port = sys.argv[1]
baud = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2].isdigit() else 115200

log(f"sending EDL cmd on {port} @ {baud}")

try:
    with serial.Serial(port, baud, timeout=2) as ser:
        ser.read(ser.in_waiting or 0)  # drain stale
        time.sleep(0.1)
        ser.write(EDL_CMD)
        ser.flush()
        log("TX >> 'platform reboot --edl' sent")

        # Read board response
        time.sleep(0.3)
        deadline = time.time() + 2.0
        total_raw = b""
        while time.time() < deadline:
            waiting = ser.in_waiting
            if waiting > 0:
                total_raw += ser.read(waiting)
            else:
                time.sleep(0.05)

        if total_raw:
            resp = total_raw.decode("utf-8", errors="replace").strip()
            log(f"RX << BOARD: {resp}")
        else:
            log("RX << no response (board may lack EDL listener or already in EDL)")

except serial.SerialException as e:
    log(f"serial error: {e} (board may already be in EDL)")

log("done")
