# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

import os
import re
import shlex
import subprocess
import time


BULK_SIZE = 256 * 1024
BULK_COUNT = 480
BULK_SG = 8
BULK_RESULTS = {}
TEST_FAILURE_RE = re.compile(r"test\s+(\d+)\s+-->\s+\d+\s+\(error\s+\d+\)")
TEST_DURATION_RE = re.compile(r"test\s+(\d+),\s+(\d+\.\d+)\s+secs")


def run_test(testusb, usb_host, device, test, *args):
    test_command = [testusb, "-D", device, "-t", str(test), *map(str, args)]
    if usb_host:
        ssh_options = shlex.split(os.getenv("SSH_OPTIONS", ""))
        remote_script = (
            "import subprocess, time\n"
            "command = ['sudo', '-n', " +
            ", ".join(repr(item) for item in test_command) + "]\n"
            "start = time.perf_counter()\n"
            "result = subprocess.run(command, text=True, capture_output=True)\n"
            "elapsed = time.perf_counter() - start\n"
            "print('__TESTUSB_ELAPSED__={:.6f}'.format(elapsed))\n"
            "print(result.stdout, end='')\n"
            "import sys\n"
            "print(result.stderr, end='', file=sys.stderr)\n"
            "raise SystemExit(result.returncode)\n"
        )
        command = ["ssh", *ssh_options, usb_host, "python3", "-"]
        result = subprocess.run(command, input=remote_script, text=True,
                                capture_output=True, timeout=600)
        elapsed_match = re.search(r"__TESTUSB_ELAPSED__=([0-9.]+)",
                                  result.stdout)
        assert elapsed_match, "Pi did not report testusb duration\n" + result.stdout
        elapsed = float(elapsed_match.group(1))
    else:
        start = time.perf_counter()
        result = subprocess.run(test_command, text=True, capture_output=True,
                                timeout=600)
        elapsed = time.perf_counter() - start

    output = result.stdout + result.stderr
    failure_match = TEST_FAILURE_RE.search(output)
    assert not failure_match, (
        "testusb case {} failed: {}".format(test, output))
    assert result.returncode == 0, output

    # The wall-clock interval ends only after testusb has returned, which
    # covers the synchronous usbtest ioctl.  Some testusb builds also print
    # the duration measured inside the kernel test; retain the larger value
    # so the reported interval cannot undercount the transfer itself.
    duration_match = TEST_DURATION_RE.search(output)
    if duration_match and int(duration_match.group(1)) == test:
        reported_elapsed = float(duration_match.group(2))
        elapsed = max(elapsed, reported_elapsed)
    return elapsed


def test_control_descriptor(testusb, usb_host, usb_device):
    run_test(testusb, usb_host, usb_device, 0)


def test_high_speed(usb_speed):
    print("Negotiated USB speed: {} Mbps".format(usb_speed))
    assert usb_speed == "480", f"USB device did not negotiate High-Speed: {usb_speed!r}"


def test_bulk_stress(testusb, usb_host, usb_device):
    run_bulk_test("Bulk OUT", testusb, usb_host, usb_device, 5)


def test_bulk_in_throughput(testusb, usb_host, usb_device):
    run_bulk_test("Bulk IN", testusb, usb_host, usb_device, 6)


def test_bulk_out_vary(testusb, usb_host, usb_device):
    run_bulk_test("Bulk OUT vary", testusb, usb_host, usb_device, 7)


def test_bulk_in_vary(testusb, usb_host, usb_device):
    run_bulk_test("Bulk IN vary", testusb, usb_host, usb_device, 8)


def run_bulk_test(label, testusb, usb_host, usb_device, test_number):
    elapsed = run_test(testusb, usb_host, usb_device, test_number,
                       "-s", BULK_SIZE, "-c", BULK_COUNT, "-g", BULK_SG)
    # usbtest 5/6 build one scatter-gather URB per iteration out of BULK_SG
    # entries of BULK_SIZE bytes, so a single direction moves
    # sglen * size * iterations bytes.  Tests 7/8 are the same transfers with
    # per-entry lengths varied by the kernel default, so their byte count is
    # not known here and only the duration is tracked.  None of the four tests
    # is bidirectional: 5/7 are OUT only and 6/8 are IN only.
    direction, varying = {
        5: ("OUT", False),
        6: ("IN", False),
        7: ("OUT", True),
        8: ("IN", True),
    }[test_number]
    print("BULK_META label={} test={} direction={} urbs={} entries_per_urb={} "
          "bytes_per_entry={} varying_length={}".format(
          label, test_number, direction, BULK_COUNT, BULK_SG, BULK_SIZE,
          varying))

    if varying:
        BULK_RESULTS[label] = None
        print("BULK_RESULT label={} test={} elapsed_s={:.3f} "
              "throughput_mib_s=n/a".format(label, test_number, elapsed))
        return

    total_bytes = BULK_SIZE * BULK_SG * BULK_COUNT
    throughput = total_bytes / elapsed / (1024 * 1024)
    BULK_RESULTS[label] = throughput
    print("BULK_RESULT label={} test={} total_mib={:.2f} elapsed_s={:.3f} "
          "throughput_mib_s={:.2f}".format(
          label, test_number, total_bytes / (1024 * 1024), elapsed,
          throughput))


def test_z_bulk_bottleneck_summary():
    print("BULK_SUMMARY results={}".format(BULK_RESULTS))

    out_rate = BULK_RESULTS.get("Bulk OUT")
    in_rate = BULK_RESULTS.get("Bulk IN")
    if out_rate is None or in_rate is None:
        print("BULK_DIAG incomplete matrix; inspect the failed test first")
        return

    if min(out_rate, in_rate) < max(out_rate, in_rate) * 0.5:
        bottleneck = "direction-specific UDC/DMA path"
    else:
        bottleneck = "common testusb/application or device-side overhead"
    print("BULK_DIAG likely_bottleneck={}".format(bottleneck))


def test_halt_recovery(testusb, usb_host, usb_device):
    run_test(testusb, usb_host, usb_device, 29, "-c", 20)
