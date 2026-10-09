# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
"""
conftest.py — pytest fixtures for SPI Shell dual-board testing via twister.

This file is auto-discovered by pytest. It provides:
  - Custom CLI options (--master, --slave, --baud, --log-uart)
  - Fixtures for master_board (via twister's dut) and slave_board (via ShellBoard)
  - Logger fixture

Usage with twister:
    west twister -p qcc748m_evk \
        -T qapp/qcc74x/qcc74x_testing/tests/drivers/spi/pytest_shell \
        --device-testing

Usage standalone:
    pytest test_spi_shell.py --master <master-port> --slave <slave-port> -v
"""

import pytest
import os
import subprocess
import sys
import time
from datetime import datetime

# Add current directory to path so we can import ShellBoard from the original module
sys.path.insert(0, os.path.dirname(__file__))

from spi_shell_test_utils import ShellBoard, TestLogger, set_logger


# ─── CLI Options ────────────────────────────────────────────────────────────

def pytest_addoption(parser):
    parser.addoption("--master", default=None,
                     help="UART port for Master board (or use SPI_MASTER_PORT)")
    parser.addoption("--slave", default=None,
                     help="UART port for Slave board (or use SPI_SLAVE_PORT)")
    parser.addoption("--baud", type=int, default=115200,
                     help="Baud rate (default: 115200)")
    parser.addoption("--log-uart", default=None, metavar="FILE",
                     help="Save UART log to file")


# ─── Collection: skip tests based on hardware config ────────────────────────

def pytest_collection_modifyitems(config, items):
    """Skip dual-board tests when no slave port is configured."""
    has_slave = bool(config.getoption("--slave") or os.environ.get("SPI_SLAVE_PORT"))

    for item in items:
        markers = {m.name for m in item.iter_markers()}
        if "dual_board" in markers and not has_slave:
            item.add_marker(pytest.mark.skip(
                reason="dual-board test requires --slave"))


# ─── Twister DUT adapter ────────────────────────────────────────────────────

class DutShellBoard:
    """Adapter wrapping twister's 'dut' to provide send_cmd() like ShellBoard."""

    def __init__(self, dut, name="master"):
        self.dut = dut
        self.name = name

    def send_cmd(self, cmd, timeout=10.0, settle_time=0.1):
        self.dut.clear_buffer()
        self.dut.write(f"{cmd}\n".encode())
        lines = []
        deadline = time.time() + timeout
        found_result = False
        while time.time() < deadline and not found_result:
            try:
                raw_line = self.dut.readline(timeout=1.0)
                if raw_line:
                    line = raw_line.strip()
                    if line == cmd or line.endswith("~$") or line.endswith("~#"):
                        continue
                    lines.append(line)
                    if line.startswith("OK:") or line.startswith("ERROR:"):
                        found_result = True
            except Exception:
                pass
        if found_result:
            # Brief wait for any trailing output (RX:, TX:, DATA:, etc.)
            if settle_time:
                time.sleep(settle_time)
            for extra in self.dut.readlines():
                extra = extra.strip()
                if extra and not extra.endswith("~$") and not extra.endswith("~#"):
                    lines.append(extra)
        return "\n".join(lines)

    def send_cmd_timed(self, cmd, timeout=10.0):
        """Return the response and host-observed command round-trip time."""
        start = time.perf_counter()
        response = self.send_cmd(cmd, timeout=timeout, settle_time=0.0)
        return response, time.perf_counter() - start

    def send_cmd_async(self, cmd):
        self.dut.write(f"{cmd}\n".encode())

    def close(self):
        pass


# ─── Fixtures ───────────────────────────────────────────────────────────────

_slave_flashed = False


def _flash_twister_slave(dut, slave_port):
    """Flash the Twister-built image to the second board before tests start."""
    global _slave_flashed

    if _slave_flashed or slave_port is None:
        return

    build_dir = str(dut.device_config.build_dir)
    command = [
        "west", "flash", "--no-rebuild", "--build-dir", build_dir,
        "--runner", "qconn_flash", "--", "--port", slave_port, "--reset",
    ]
    try:
        subprocess.run(command, check=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        pytest.fail(f"Failed to flash SPI slave on {slave_port}: {exc}")

    _slave_flashed = True


@pytest.fixture(scope="session")
def uart_logger(request):
    """Session-scoped UART logger."""
    log_file = request.config.getoption("--log-uart")
    if log_file is None:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        log_file = f"spi_test_{timestamp}.log"
    verbose = request.config.getoption("-v", default=0) > 0
    logger = TestLogger(verbose=verbose, log_file=log_file)
    set_logger(logger)
    yield logger
    logger.close()


@pytest.fixture(scope="function")
def master_board(request, uart_logger):
    """Master board via Twister DUT, or a serial ShellBoard in standalone pytest."""
    try:
        dut = request.getfixturevalue("dut")
    except pytest.FixtureLookupError:
        dut = None

    if dut is not None:
        board = DutShellBoard(dut, "master")
    else:
        port = os.environ.get("SPI_MASTER_PORT") or request.config.getoption("--master")
        if port is None:
            pytest.fail("standalone pytest requires --master COMx")
        baud = request.config.getoption("--baud")
        board = ShellBoard(port, baud, "master")
        board.open()

    yield board
    board.close()


@pytest.fixture(scope="session")
def slave_board(request, uart_logger):
    """Slave board via one persistent ShellBoard session.

    Reopening the slave UART for every test can toggle/reset the board and
    eventually leave the shell unresponsive.  Keep the connection open for
    the whole pytest session; the autouse fixture still restores SPI settings
    before every test.
    """
    port = os.environ.get("SPI_SLAVE_PORT") or request.config.getoption("--slave")
    if port is None:
        yield None
        return

    # Flash the slave before opening its UART.  Opening the UART first races with
    # qconn_flash, which then fails with ERROR_ACCESS_DENIED.  The master DUT
    # fixture must not perform this step because pytest is free to initialize
    # the slave fixture before the master fixture.
    try:
        dut = request.getfixturevalue("dut")
    except pytest.FixtureLookupError:
        dut = None
    if dut is not None:
        _flash_twister_slave(dut, port)

    baud = request.config.getoption("--baud")
    board = ShellBoard(port, baud, "slave")
    board.open()
    yield board
    board.close()


@pytest.fixture(scope="function", autouse=True)
def spi_init(master_board, slave_board):
    """Configure SPI defaults on both boards before each test function."""
    # Serial open/reset has already completed in the board fixture.  A long
    # sleep here multiplied the suite runtime by the number of pytest cases.
    time.sleep(0.1)
    # Restore all runtime parameters here so every test starts deterministic.
    resp = master_board.send_cmd(
        "qcom spi config -r master -m 0 -f 1000000 -w 8 -o msb")
    assert "OK:" in resp, f"Master SPI baseline config failed: {resp}"

    if slave_board is not None:
        resp = slave_board.send_cmd(
            "qcom spi config -r slave -m 0 -f 1000000 -w 8 -o msb")
        assert "OK:" in resp, f"Slave SPI baseline config failed: {resp}"

    yield

