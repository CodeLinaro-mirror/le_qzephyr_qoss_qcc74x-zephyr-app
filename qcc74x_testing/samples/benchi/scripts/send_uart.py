# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/usr/bin/env python3

import argparse
import serial
import time

def send_command(device, baudrate, command_list, wait_pattern=None, wait_max=None, verbose=False):
    command = ""

    # put a single quote around each argument
    for cmd in command_list:
        command += "'%s' " % cmd

    retcode = 1
    print(f"Sending UART command: {command}")
    try:
        with serial.Serial(device, baudrate, timeout=0.1) as ser:
            ser.reset_input_buffer()
            ser.write((command + "\n").encode())
            ser.write(b"\r")

            if verbose:
                print(f"Waiting for pattern '{wait_pattern}' ...")
            start_time = time.time()
            buffer=""
            if wait_pattern:
                pattern_found = False
                while True:
                    if wait_max and (time.time() - start_time) > wait_max:
                        print("Timeout reached while waiting for pattern.")
                        break
                    data = ser.read(1024).decode(errors='ignore') # read 1024 bytes chunks
                    if data:
                        buffer += data
                        if verbose:
                            print(data, end="", flush=True)  # Print as data arrives
                        if wait_pattern in buffer:
                            pattern_found = True
                            break
                if pattern_found:
                    if verbose:
                        print("\n--> pattern found", flush=True)
                    retcode = 0
            else:
                retcode = 0

    except serial.SerialException as e:
        print(f"Error: {e}")

    return retcode

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Send commands via UART shell.")
    parser.add_argument("-b", "--baudrate", type=int, required=True, help="Baud rate for UART communication.")
    parser.add_argument("--delay", type=float, help="Delay time after sending the uart command.")
    parser.add_argument("--dev", type=str, required=True, help="Device path (e.g., /dev/ttyACM0).")
    parser.add_argument("--wait", type=str, default="uart:~", help="Wait for receiving a specific pattern after command is sent")
    parser.add_argument("--wait-max", type=float, default=2, help="Wait maximum time (timout)")
    parser.add_argument("--verbose", action="store_true", help="Show more verbose log messages")
    parser.add_argument("command", nargs='+', help="Command to execute on the UART shell.")

    args = parser.parse_args()
    exit_code = send_command(args.dev, args.baudrate, args.command, args.wait, args.wait_max, args.verbose)
    if args.delay:
        time.sleep(args.delay)
    exit(exit_code)

