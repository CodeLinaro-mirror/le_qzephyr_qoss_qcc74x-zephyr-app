# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

"""USB CDC ACM host fixtures."""

import os
import base64
import shlex
import subprocess
import time

import pytest
import serial


def pytest_addoption(parser):
    parser.addoption("--usb-port", default=os.getenv("USB_PORT"),
                     help="Host USB CDC ACM port (COMx locally or /dev/ttyACMx remotely)")
    parser.addoption("--usb-host", default=os.getenv("USB_HOST"),
                     help="SSH host running the USB CDC ACM host, for example pi@raspberrypi")
    parser.addoption("--usb-enum-timeout", default=os.getenv("USB_ENUM_TIMEOUT", "30"),
                     help="Seconds to wait for the USB CDC ACM port after flashing")


@pytest.fixture(scope="session")
def usb_port(request):
    port = request.config.getoption("--usb-port")
    if not port:
        port = "/dev/ttyACM0" if request.config.getoption("--usb-host") else None
    if not port:
        pytest.skip("USB_PORT or --usb-port is required for hardware testing")
    return port


class RemoteCdcAcm:
    """Run one CDC ACM exchange on a Linux USB host over SSH."""

    def __init__(self, host, port, ssh_options, timeout):
        self.host = host
        self.port = port
        self.ssh_options = shlex.split(ssh_options)
        self.timeout = timeout
        remote_script = (
            "import serial, sys, time\n"
            "with serial.Serial(sys.argv[1], 115200, timeout=2, write_timeout=5,\n"
            "                   xonxoff=False, rtscts=False, dsrdtr=False) as ser:\n"
            "    ser.dtr = True\n"
            "    ser.rts = True\n"
            "    ser.reset_input_buffer()\n"
            "    for line in sys.stdin:\n"
            "        payload = bytes.fromhex(line.strip())\n"
        "        ser.reset_input_buffer()\n"
        "        ser.write(payload)\n"
        "        ser.flush()\n"
            "        received = ser.read(len(payload))\n"
            "        print(received.hex(), flush=True)\n"
        )
        encoded_script = base64.b64encode(remote_script.encode()).decode()
        remote_command = (
            "python3 -c \"import base64;exec(base64.b64decode('" +
            encoded_script + "'))\" " + shlex.quote(self.port)
        )
        command = ["ssh", *self.ssh_options, self.host, remote_command]
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE, text=True)
        time.sleep(0.5)

    def exchange(self, payload):
        if self.process.poll() is not None:
            error = self.process.stderr.read()
            raise AssertionError(error)
        self.process.stdin.write(payload.hex() + "\n")
        self.process.stdin.flush()
        received = self.process.stdout.readline()
        if not received:
            raise AssertionError(self.process.stderr.read())
        return bytes.fromhex(received.strip())

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=self.timeout)


@pytest.fixture(scope="session")
def usb(request, usb_port):
    timeout = float(request.config.getoption("--usb-enum-timeout"))
    usb_host = request.config.getoption("--usb-host")
    if usb_host:
        connection = RemoteCdcAcm(usb_host, usb_port,
                                  os.getenv("SSH_OPTIONS", ""), timeout)
        yield connection
        connection.close()
        return

    deadline = time.monotonic() + timeout
    ser = None

    # The USB CDC ACM COM port does not exist until after Twister flashes
    # and the device completes USB enumeration.  Retry opening it instead
    # of treating the expected pre-enumeration absence as a test error.
    while time.monotonic() < deadline:
        try:
            ser = serial.Serial(usb_port, 115200, timeout=2, write_timeout=5)
            break
        except serial.SerialException:
            time.sleep(0.5)

    if ser is None:
        pytest.fail(f"USB CDC ACM port {usb_port} did not enumerate within {timeout:.1f}s")

    time.sleep(0.5)
    ser.reset_input_buffer()
    class LocalCdcAcm:
        def exchange(self, payload):
            ser.reset_input_buffer()
            ser.write(payload)
            ser.flush()
            return ser.read(len(payload))

    yield LocalCdcAcm()
    ser.close()
