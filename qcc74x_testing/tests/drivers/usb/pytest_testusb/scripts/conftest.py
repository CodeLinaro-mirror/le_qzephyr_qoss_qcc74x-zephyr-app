# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

import os
import re
import shlex
import subprocess

import pytest


def pytest_addoption(parser):
    parser.addoption("--usb-device", default=os.getenv("USB_DEVICE"),
                     help="Linux USB device node, e.g. /dev/bus/usb/003/017")
    parser.addoption("--usb-host", default=os.getenv("USB_HOST"),
                     help="SSH host running testusb, e.g. pi@raspberrypi")
    parser.addoption("--testusb", default=os.getenv("TESTUSB", "testusb"),
                     help="Path to the Linux testusb executable")


@pytest.fixture(scope="session")
def usb_device(request):
    value = request.config.getoption("--usb-device")
    if value and value.lower() != "auto":
        return value

    host = request.getfixturevalue("usb_host")
    if not host:
        pytest.skip("USB_HOST is required for automatic USB device discovery")

    command = ["ssh", *shlex.split(os.getenv("SSH_OPTIONS", "")), host,
               "lsusb -d 2fe3:0009"]
    result = subprocess.run(command, text=True, capture_output=True, timeout=30)
    match = re.search(r"Bus\s+(\d+)\s+Device\s+(\d+):\s+ID\s+2fe3:0009",
                      result.stdout)
    if result.returncode != 0 or not match:
        pytest.fail("QCC748M USB device 2fe3:0009 was not found on the Raspberry Pi\n"
                    + result.stdout + result.stderr)
    return f"/dev/bus/usb/{int(match.group(1)):03d}/{int(match.group(2)):03d}"


@pytest.fixture(scope="session")
def testusb(request):
    return request.config.getoption("--testusb")


@pytest.fixture(scope="session")
def usb_host(request):
    return request.config.getoption("--usb-host")


@pytest.fixture(scope="session")
def usb_speed(usb_host, usb_device):
    """Return the negotiated USB speed reported by the Raspberry Pi."""
    if not usb_host:
        pytest.skip("USB_HOST is required for USB speed detection")

    ssh_options = shlex.split(os.getenv("SSH_OPTIONS", ""))
    path_command = ["ssh", *ssh_options, usb_host,
                    "udevadm", "info", "--query=path", "--name", usb_device]
    path_result = subprocess.run(path_command, text=True, capture_output=True,
                                 timeout=30)
    if path_result.returncode != 0:
        pytest.fail("Could not resolve USB sysfs path on the Raspberry Pi\n" +
                    path_result.stdout + path_result.stderr)

    sysfs_path = path_result.stdout.strip()
    speed_command = ["ssh", *ssh_options, usb_host,
                     "cat", "/sys" + sysfs_path + "/speed"]
    speed_result = subprocess.run(speed_command, text=True, capture_output=True,
                                  timeout=30)
    if speed_result.returncode != 0:
        pytest.fail("Could not read USB speed on the Raspberry Pi\n" +
                    speed_result.stdout + speed_result.stderr)

    return speed_result.stdout.strip()
