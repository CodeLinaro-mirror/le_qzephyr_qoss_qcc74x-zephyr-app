# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
"""
spi_shell_test_utils.py
Automated SPI dual-board (Master/Slave) and single-board loopback test.

Controls two QCC748M EVK boards via UART shell:
  - Board A (Master): drives clock, sends/receives data
  - Board B (Slave): responds to master clock

Hardware Setup:
  Single-board loopback: MOSI(GPIO31) -> MISO(GPIO34) jumper on one board
  Dual-board: Connect Board_A to Board_B:
    Master SCLK(GPIO13)  <-> Slave SCLK(GPIO13)
    Master MOSI(GPIO11)  <-> Slave MOSI(GPIO11)
    Master MISO(GPIO10)  <-> Slave MISO(GPIO10)
    Master SS(GPIO12)    <-> Slave SS(GPIO12)

Usage:
    python spi_shell_test_utils.py --master <master-port> --slave <slave-port>
    python spi_shell_test_utils.py --master <master-port>       # single-board loopback only
    python spi_shell_test_utils.py --master <master-port> --slave <slave-port>

Requirements:
    pip install pyserial
"""

import sys
import time
import re
import argparse
import threading
import serial
import os
from datetime import datetime


# ─── Logger ─────────────────────────────────────────────────────────────────

class TestLogger:
    """Records all UART commands and responses for debugging."""

    def __init__(self, verbose=False, log_file=None):
        self.verbose = verbose
        self.log_file = None
        self.entries = []
        if log_file:
            self.log_file = open(log_file, "w", encoding="utf-8")
            self.log_file.write(f"# SPI Test Log - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")

    def log(self, board_name, direction, content):
        """Log a command or response.

        direction: 'TX' (command sent) or 'RX' (response received)
        """
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        entry = f"[{timestamp}] [{board_name}] {direction}: {content}"
        self.entries.append(entry)
        if self.verbose:
            print(f"    {entry}")
        if self.log_file:
            self.log_file.write(entry + "\n")
            self.log_file.flush()

    def close(self):
        if self.log_file:
            self.log_file.write(f"\n# End of log - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            self.log_file.close()


# Global logger instance
_logger = TestLogger()


def set_logger(logger):
    global _logger
    _logger = logger


# ANSI escape code regex
_ANSI_RE = re.compile(r'\x1b\[[0-9;]*[A-Za-z]|\x1b\].*?\x07')


def _strip_ansi(text):
    """Remove ANSI escape codes from text."""
    return _ANSI_RE.sub('', text).strip()


# ─── UART Shell Helpers ─────────────────────────────────────────────────────

class ShellBoard:
    """Represents one board connected via UART shell."""

    def __init__(self, port, baudrate=115200, name="board"):
        self.name = name
        self.port = port
        self.baudrate = baudrate
        self.ser = None

    def open(self):
        self.ser = serial.Serial(
            port=None, baudrate=self.baudrate, timeout=1, write_timeout=2,
            rtscts=False, dsrdtr=False,
        )
        self.ser.port = self.port
        self.ser.rts = False
        self.ser.dtr = True
        self.ser.open()
        time.sleep(0.5)
        self.ser.reset_input_buffer()
        # Send empty line to sync
        self._raw_send("")
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def _raw_send(self, cmd):
        self.ser.write(f"{cmd}\r\n".encode())
        self.ser.flush()

    def send_cmd(self, cmd, timeout=5.0, settle_time=0.1):
        """Send command and wait for response lines until prompt or timeout."""
        self.ser.reset_input_buffer()
        self._raw_send(cmd)
        _logger.log(self.name, "TX", cmd)

        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            raw_line = self.ser.readline().decode("utf-8", errors="replace").strip()
            # Strip ANSI escape codes
            line = _strip_ansi(raw_line)
            if line:
                # Skip command echo and shell prompt lines
                if line == cmd or line.endswith("~$") or line.endswith("~#"):
                    continue
                lines.append(line)
                # Check if we got a result line
                if line.startswith("OK:") or line.startswith("ERROR:"):
                    # Read any additional output lines (DATA:, TX:, RX:, etc.)
                    if settle_time:
                        time.sleep(settle_time)
                    while self.ser.in_waiting:
                        extra_raw = self.ser.readline().decode("utf-8", errors="replace").strip()
                        extra = _strip_ansi(extra_raw)
                        if extra and not extra.endswith("~$") and not extra.endswith("~#"):
                            lines.append(extra)
                    break
        response = "\n".join(lines)
        _logger.log(self.name, "RX", response.replace("\n", " | "))
        return response

    def send_cmd_timed(self, cmd, timeout=5.0):
        """Return the response and host-observed command round-trip time."""
        start = time.perf_counter()
        response = self.send_cmd(cmd, timeout=timeout, settle_time=0.0)
        return response, time.perf_counter() - start

    def send_cmd_async(self, cmd):
        """Send command without waiting for response (for slave_prepare)."""
        self.ser.reset_input_buffer()
        self._raw_send(cmd)
        _logger.log(self.name, "TX", f"{cmd}  (async, no wait)")
        time.sleep(0.05)

    def read_response(self, timeout=10.0):
        """Read response lines until OK/ERROR or timeout."""
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.ser.in_waiting:
                line = self.ser.readline().decode("utf-8", errors="replace").strip()
                if line:
                    lines.append(line)
                    if line.startswith("OK:") or line.startswith("ERROR:"):
                        time.sleep(0.1)
                        while self.ser.in_waiting:
                            extra = self.ser.readline().decode("utf-8", errors="replace").strip()
                            if extra:
                                lines.append(extra)
                        break
            else:
                time.sleep(0.01)
        return "\n".join(lines)


def check_ok(response, context=""):
    """Check if response contains OK."""
    if "OK:" in response:
        return True
    print(f"  [FAIL] {context}: {response}")
    return False


def extract_data(response):
    """Extract DATA: field from response."""
    for line in response.split("\n"):
        if line.startswith("DATA:"):
            return line.split("DATA:")[1].strip()
    return None


def extract_rx(response):
    """Extract RX: field from response."""
    for line in response.split("\n"):
        if line.startswith("RX:"):
            return line.split("RX:")[1].strip()
    return None


# ─── Test Cases ─────────────────────────────────────────────────────────────

class SPITestSuite:
    def __init__(self, master, slave=None):
        self.master = master
        self.slave = slave
        self.passed = 0
        self.failed = 0
        self.skipped = 0

    def run_all(self):
        """Run applicable tests based on configuration.

        - Single-board (master only): Phase 1 loopback tests (MOSI->MISO jumper)
        - Dual-board (master + slave): Phase 2 master/slave tests (two boards wired)
        """
        mode_str = "Dual-Board" if self.slave else "Single-Board Loopback"
        print(f"\n{'='*70}")
        print(f" SPI Test Suite — {mode_str}")
        print(f" Master: {self.master.port}")
        if self.slave:
            print(f" Slave:  {self.slave.port}")
        print(f"{'='*70}\n")

        if self.slave:
            # Dual-board: only run Phase 2 (master/slave tests)
            self._run_dual_board_tests()
        else:
            # Single-board: only run Phase 1 (loopback tests)
            self._run_single_board_tests()

        # Summary
        total = self.passed + self.failed
        print(f"\n{'='*70}")
        print(f" RESULTS: {self.passed}/{total} passed, {self.failed} failed, {self.skipped} skipped")
        if self.failed == 0:
            print(f" VERDICT: ALL PASS ✓")
        else:
            print(f" VERDICT: FAILED ✗")
        print(f"{'='*70}")
        return self.failed == 0

    # ─── Phase 1: Single-Board Loopback ─────────────────────────────────────

    def _run_single_board_tests(self):
        # Test 1.1: Basic loopback
        self._test("1.1 Basic loopback (32B, incr pattern)",
                   lambda: self._loopback_test(32, "incr", 1))

        # Test 1.2: Large loopback
        self._test("1.2 Large loopback (1024B)",
                   lambda: self._loopback_test(1024, "incr", 1))

        # Test 1.3: Multiple iterations
        self._test("1.3 Repeated loopback (64B x 10 iterations)",
                   lambda: self._loopback_test(64, "incr", 10))

        # Test 1.4: Different SPI modes
        for mode in [0, 1, 2, 3]:
            self._test(f"1.4.{mode} Loopback SPI Mode {mode}",
                       lambda m=mode: self._loopback_mode_test(m))

        # Test 1.5: Different frequencies (up to 40MHz max per datasheet)
        for freq in [500000, 1000000, 2000000, 4000000, 8000000, 10000000, 20000000, 40000000]:
            self._test(f"1.5 Loopback @ {freq//1000}kHz",
                       lambda f=freq: self._loopback_freq_test(f))

        # Test 1.6: Stress test
        self._test("1.6 Stress test (256B x 100 iterations)",
                   lambda: self._stress_test())

        # Test 1.7: Transceive with hex data
        self._test("1.7 Transceive hex data (AABBCCDD)",
                   lambda: self._transceive_hex_test())

    def _loopback_test(self, length, pattern, iterations):
        self.master.send_cmd("qcom spi config -r master -m 0 -f 1000000")
        resp = self.master.send_cmd(
            f"qcom spi loopback -l {length} -p {pattern} -n {iterations}", timeout=10)
        return "OK:" in resp and "0 errors" in resp

    def _loopback_mode_test(self, mode):
        resp = self.master.send_cmd(f"qcom spi config -r master -m {mode} -f 1000000")
        if "OK:" not in resp:
            return False
        resp = self.master.send_cmd("qcom spi loopback -l 32 -n 5", timeout=5)
        return "OK:" in resp and "0 errors" in resp

    def _loopback_freq_test(self, freq):
        resp = self.master.send_cmd(f"qcom spi config -r master -m 0 -f {freq}")
        if "OK:" not in resp:
            return False
        resp = self.master.send_cmd("qcom spi loopback -l 64 -n 3", timeout=5)
        return "OK:" in resp and "0 errors" in resp

    def _stress_test(self):
        self.master.send_cmd("qcom spi config -r master -m 0 -f 2000000")
        # Use loopback command which verifies RX == TX data integrity
        resp = self.master.send_cmd("qcom spi loopback -l 256 -p incr -n 100", timeout=30)
        return "OK:" in resp and "0 errors" in resp

    def _transceive_hex_test(self):
        self.master.send_cmd("qcom spi config -r master -m 0 -f 1000000")
        resp = self.master.send_cmd("qcom spi transceive AABBCCDD")
        if "OK:" not in resp:
            return False
        rx = extract_rx(resp)
        # In loopback, RX should equal TX
        return rx == "AABBCCDD"

    # ─── Phase 2: Dual-Board Tests ──────────────────────────────────────────

    def _run_dual_board_tests(self):
        # Test 2.1: Basic Master->Slave communication
        self._test("2.1 Master write -> Slave read (4 bytes)",
                   lambda: self._dual_basic_write())

        # Test 2.2: Full-duplex Master<->Slave
        self._test("2.2 Full-duplex transceive (4 bytes)",
                   lambda: self._dual_full_duplex())

        # Test 2.3: Larger transfer
        self._test("2.3 Full-duplex (64 bytes, incr pattern)",
                   lambda: self._dual_larger_transfer())

        # Test 2.4: All SPI modes
        for mode in [0, 1, 2, 3]:
            self._test(f"2.4.{mode} Dual-board SPI Mode {mode}",
                       lambda m=mode: self._dual_mode_test(m))

        # Test 2.5: Different frequencies (up to 40MHz max per datasheet)
        for freq in [500000, 1000000, 2000000, 4000000, 8000000, 10000000, 20000000, 40000000]:
            self._test(f"2.5 Dual-board @ {freq//1000}kHz",
                       lambda f=freq: self._dual_freq_test(f))

        # Test 2.6: Repeated Master/Slave exchanges
        self._test("2.6 Repeated exchanges (10 iterations)",
                   lambda: self._dual_repeated_test())

        # Test 2.7: Different word sizes (if supported)
        for ws in [8, 16, 32]:
            self._test(f"2.7 Dual-board word size {ws} bits",
                       lambda w=ws: self._dual_word_size_test(w))

    def _dual_basic_write(self):
        """Master writes AABBCCDD, slave should receive it."""
        # Configure both boards
        self.slave.send_cmd("qcom spi config -r slave -m 0 -f 1000000")
        self.master.send_cmd("qcom spi config -r master -m 0 -f 1000000")

        # Slave prepares (non-blocking - submits work to background thread)
        self.slave.send_cmd_async("qcom spi slave_prepare DEADBEEF")
        time.sleep(0.1)  # Give slave time to enter waiting state

        # Master writes
        resp = self.master.send_cmd("qcom spi transceive AABBCCDD")
        if "OK:" not in resp:
            return False

        # Slave reads what it received
        slave_resp = self.slave.send_cmd("qcom spi slave_read -t 3000")
        if "OK:" not in slave_resp:
            return False

        data = extract_data(slave_resp)
        return data == "AABBCCDD"

    def _dual_full_duplex(self):
        """Full-duplex: master sends AABB, slave prepared CCDD, verify both sides."""
        self.slave.send_cmd("qcom spi config -r slave -m 0 -f 1000000")
        self.master.send_cmd("qcom spi config -r master -m 0 -f 1000000")

        # Slave prepares its TX data
        self.slave.send_cmd_async("qcom spi slave_prepare CCDD1122")
        time.sleep(0.1)

        # Master transceives
        resp = self.master.send_cmd("qcom spi transceive AABB3344")
        if "OK:" not in resp:
            return False

        # Master should have received slave's data
        master_rx = extract_rx(resp)

        # Slave should have received master's data
        slave_resp = self.slave.send_cmd("qcom spi slave_read -t 3000")
        slave_data = extract_data(slave_resp)

        # Verify both sides
        master_ok = (master_rx == "CCDD1122")
        slave_ok = (slave_data == "AABB3344")

        if not master_ok:
            print(f"    Master RX mismatch: expected CCDD1122, got {master_rx}")
        if not slave_ok:
            print(f"    Slave RX mismatch: expected AABB3344, got {slave_data}")

        return master_ok and slave_ok

    def _dual_larger_transfer(self):
        """64-byte incremental pattern exchange."""
        self.slave.send_cmd("qcom spi config -r slave -m 0 -f 1000000")
        self.master.send_cmd("qcom spi config -r master -m 0 -f 1000000")

        # Slave prepares 64 bytes of 0x55
        self.slave.send_cmd_async("qcom spi slave_prepare -p fixed:0x55 -l 64")
        time.sleep(0.1)

        # Master sends incremental pattern
        resp = self.master.send_cmd("qcom spi transceive -p incr -l 64", timeout=5)
        if "OK:" not in resp:
            return False

        # Verify slave received data
        slave_resp = self.slave.send_cmd("qcom spi slave_read -t 3000")
        if "OK:" not in slave_resp:
            return False

        # Check first few bytes of slave data (should be 00010203...)
        slave_data = extract_data(slave_resp)
        if slave_data and slave_data.startswith("00010203"):
            return True
        print(f"    Slave data: {slave_data[:32] if slave_data else 'None'}...")
        return False

    def _dual_mode_test(self, mode):
        """Test a specific SPI mode in dual-board config."""
        self.slave.send_cmd(f"qcom spi config -r slave -m {mode} -f 1000000")
        self.master.send_cmd(f"qcom spi config -r master -m {mode} -f 1000000")

        self.slave.send_cmd_async("qcom spi slave_prepare A5A5A5A5")
        time.sleep(0.1)

        resp = self.master.send_cmd("qcom spi transceive 5A5A5A5A")
        if "OK:" not in resp:
            return False

        master_rx = extract_rx(resp)

        slave_resp = self.slave.send_cmd("qcom spi slave_read -t 3000")
        slave_data = extract_data(slave_resp)

        return master_rx == "A5A5A5A5" and slave_data == "5A5A5A5A"

    def _dual_freq_test(self, freq):
        """Test a specific frequency in dual-board config."""
        self.slave.send_cmd(f"qcom spi config -r slave -m 0 -f {freq}")
        self.master.send_cmd(f"qcom spi config -r master -m 0 -f {freq}")

        self.slave.send_cmd_async("qcom spi slave_prepare 12345678")
        time.sleep(0.1)

        resp = self.master.send_cmd("qcom spi transceive DEADBEEF")
        if "OK:" not in resp:
            return False

        master_rx = extract_rx(resp)
        slave_resp = self.slave.send_cmd("qcom spi slave_read -t 3000")
        slave_data = extract_data(slave_resp)

        return master_rx == "12345678" and slave_data == "DEADBEEF"

    def _dual_repeated_test(self):
        """Repeat master/slave exchange multiple times."""
        self.slave.send_cmd("qcom spi config -r slave -m 0 -f 2000000")
        self.master.send_cmd("qcom spi config -r master -m 0 -f 2000000")

        for i in range(10):
            tx_data = f"{i:02X}" * 4  # e.g., "00000000", "01010101", etc.
            slave_data = f"{(0xFF - i):02X}" * 4

            self.slave.send_cmd_async(f"qcom spi slave_prepare {slave_data}")
            time.sleep(0.05)

            resp = self.master.send_cmd(f"qcom spi transceive {tx_data}")
            if "OK:" not in resp:
                print(f"    Failed at iteration {i}")
                return False

            master_rx = extract_rx(resp)
            sr = self.slave.send_cmd("qcom spi slave_read -t 3000")
            sd = extract_data(sr)

            if master_rx != slave_data or sd != tx_data:
                print(f"    Mismatch at iter {i}: master_rx={master_rx} slave_rx={sd}")
                return False

        return True

    def _dual_word_size_test(self, word_size):
        """Test different word sizes."""
        self.slave.send_cmd(f"qcom spi config -r slave -m 0 -f 1000000 -w {word_size}")
        self.master.send_cmd(f"qcom spi config -r master -m 0 -f 1000000 -w {word_size}")

        # Use data length matching word size
        if word_size == 8:
            tx = "AABBCCDD"
            stx = "11223344"
        elif word_size == 16:
            tx = "AABBCCDD"
            stx = "11223344"
        else:  # 32
            tx = "AABBCCDD"
            stx = "11223344"

        self.slave.send_cmd_async(f"qcom spi slave_prepare {stx}")
        time.sleep(0.1)

        resp = self.master.send_cmd(f"qcom spi transceive {tx}")
        if "OK:" not in resp:
            return False

        master_rx = extract_rx(resp)
        slave_resp = self.slave.send_cmd("qcom spi slave_read -t 3000")
        slave_data = extract_data(slave_resp)

        return master_rx == stx and slave_data == tx

    # ─── Test Runner Helper ──────────────────────────────────────────────────

    def _test(self, name, fn):
        """Run a single test case."""
        # Log test name as a comment/separator
        _logger.log("test", "--", f"[{name}]")
        if _logger.verbose:
            # In verbose mode, print test name on its own line before log output
            print(f"  [{name}] ", end="", flush=True)
            print()  # newline so verbose log lines appear below
        else:
            sys.stdout.write(f"  [{name}] ")
            sys.stdout.flush()
        try:
            result = fn()
            if result:
                if _logger.verbose:
                    print(f"  -> PASS ✓")
                else:
                    print("PASS ✓")
                self.passed += 1
            else:
                if _logger.verbose:
                    print(f"  -> FAIL ✗")
                else:
                    print("FAIL ✗")
                self.failed += 1
        except Exception as e:
            if _logger.verbose:
                print(f"  -> ERROR: {e}")
            else:
                print(f"ERROR: {e}")
            self.failed += 1


# ─── Main ───────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="qcom spi Dual-Board Test Suite for QCC748M EVK")
    parser.add_argument("--master", required=True,
                        help="UART port for Master board")
    parser.add_argument("--slave", default=None,
                        help="UART port for Slave board")
    parser.add_argument("--baud", type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Print all UART commands and responses in real-time")
    parser.add_argument("--log", default=None, metavar="FILE",
                        help="Save UART command/response log to file (e.g., --log spi_test.log)")
    args = parser.parse_args()

    # Set up logger
    log_file = args.log
    if log_file is None:
        # Auto-generate log filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        log_file = f"spi_test_{timestamp}.log"
    logger = TestLogger(verbose=args.verbose, log_file=log_file)
    set_logger(logger)

    master = ShellBoard(args.master, args.baud, "master")
    slave = ShellBoard(args.slave, args.baud, "slave") if args.slave else None

    try:
        print(f"[*] Opening Master UART: {args.master}")
        master.open()

        if slave:
            print(f"[*] Opening Slave UART: {args.slave}")
            slave.open()

        print(f"[*] Log file: {os.path.abspath(log_file)}")

        suite = SPITestSuite(master, slave)
        success = suite.run_all()

    finally:
        master.close()
        if slave:
            slave.close()
        logger.close()
        print(f"\n[*] Full command log saved to: {os.path.abspath(log_file)}")

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()


