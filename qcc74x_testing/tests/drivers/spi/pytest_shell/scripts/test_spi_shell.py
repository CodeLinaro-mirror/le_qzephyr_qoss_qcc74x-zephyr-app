# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
"""Focused L4 SPI shell tests.

L0 and L2 own exhaustive API, parameter-matrix, stress and performance
coverage. L4 verifies the shell command surface and a representative exchange
in each transport mode.
"""

import logging
import pytest
import re
import time

from collections import Counter

from spi_shell_test_utils import extract_data, extract_rx


MODE_DIAGNOSTIC_FREQUENCY_HZ = 1_000_000
THROUGHPUT_BLOCK_LEN = 65536
THROUGHPUT_BLOCKS = 4
THROUGHPUT_BYTES = THROUGHPUT_BLOCK_LEN * THROUGHPUT_BLOCKS
WORD_SIZE_DIAGNOSTIC_FREQUENCY_HZ = 100_000
SLAVE_READY_DELAY_S = 1.0
# ─── Bidirectional throughput sweep ─────────────────────────────────────────
# The throughput run is already a full-duplex transceive, so both directions are
# on the wire; only the master's RX buffer went unchecked. `-e` makes the master
# compare it against the fixed byte the slave was armed with, which turns the
# same run into a read-direction measurement at no extra cost.
SWEEP_FREQUENCIES_HZ = [2_000_000, 20_000_000, 26_000_000, 40_000_000]
# The read direction has no margin at 40 MHz (see md/spi/40mhz-read-failure.md),
# so a read error there is recorded, not failed on. Every other step must be
# clean in both directions.
SWEEP_READ_TOLERATED_HZ = {40_000_000}
SWEEP_PAYLOAD_LEN = 65536
SWEEP_BLOCKS = 4
SWEEP_SLAVE_TX_BYTE = 0xA5

# ─── DMA read-direction (MISO) check ────────────────────────────────────────
# The throughput case only proves the write direction: the slave verifies what
# the master sent, while the master's own RX buffer is never checked. These
# constants drive a full-duplex case that checks the DMA RX buffer instead.
READ_DIRECTION_SLAVE_TX = "12345678"
READ_DIRECTION_MASTER_TX = "DEADBEEF"
READ_DIRECTION_ITERATIONS = 20
# 1 MHz is the control point: a failure there would mean the DMA read path is
# broken outright rather than short of sampling margin at the top frequency.
READ_DIRECTION_CONTROL_FREQUENCY_HZ = 1_000_000
# The highest step selected for the supported MISO/read-direction test: the
# 160 MHz source gives 160 MHz / 6 = 26.67 MHz, an 18.75 ns sampling window.
# Requesting 26 MHz makes the driver select that pair. 40 MHz is intentionally
# excluded because the return path is known to be marginal on this board.
READ_DIRECTION_SUPPORTED_FREQUENCY_HZ = 26_000_000

# ─── DMA read-margin diagnostic ──────────────────────────────────────────────
# A 4-byte exchange yields 32 bits of evidence, which is too little to tell a
# startup problem (slave armed late, so only the first bits are wrong) from a
# per-bit sampling problem (wrong bits anywhere in the payload). These drive a
# 64-byte exchange, 512 bits per sample, across the SCLK steps the 160 MHz
# source can produce.
#
# Random-looking payload on purpose: the check below is only falsifiable at bit
# positions where the slave's stream changes value, so a pattern with a high
# transition density is what gives the result its power. 265 of 511 boundaries
# in this pattern are transitions.
READ_MARGIN_SLAVE_TX = (
    "1FDC6D3D0FF3296628F9E51C20AAFE16E736F2E1224C754136767EA852A34436"
    "5472E7378CA11B13A777D9EC234D4857266EDDD6272C6163B9572E6BD716206D"
)
READ_MARGIN_ITERATIONS = 30
# spi_clk stays at 160 MHz and SCLK = spi_clk / (2 * (PRD_D + 1)), so the
# reachable steps are 40, 26.67, 20 and 16 MHz. Requesting the values below
# lands on those steps, halving and quartering the sampling window.
READ_MARGIN_FREQUENCIES_HZ = [40_000_000, 26_000_000, 20_000_000, 16_000_000]

_log = logging.getLogger(__name__)


@pytest.mark.single_board
class TestSingleBoardShell:
    """One-board command and functional coverage."""

    def test_1_1_command_smoke(self, master_board):
        """Cover the status and configuration commands exposed by spi_shell."""
        status = master_board.send_cmd("qcom spi status")
        assert "qcom spi Status:" in status
        assert "State: ready" in status

        resp = master_board.send_cmd(
            f"qcom spi config -r master -m 0 -f {MODE_DIAGNOSTIC_FREQUENCY_HZ}")
        assert "OK:" in resp


    def test_1_2_invalid_config(self, master_board):
        """Cover shell validation."""
        resp = master_board.send_cmd("qcom spi config -r master -m 0 -f 1000000 -w 12")
        assert "ERROR: invalid word size 12" in resp

        resp = master_board.send_cmd("qcom spi config -r master -m 0 -f 0")
        assert "ERROR: invalid frequency 0" in resp
        resp = master_board.send_cmd("qcom spi transceive AABBCCDD")
        assert "ERROR:" in resp

    def test_1_8_transceive_hex_data(self, master_board):
        master_board.send_cmd("qcom spi config -r master -m 0 -f 1000000")
        resp = master_board.send_cmd("qcom spi transceive AABBCCDD")
        assert "OK:" in resp
        assert extract_rx(resp) == "AABBCCDD"


@pytest.mark.dual_board
class TestDualBoardShell:
    """Focused two-board command and data-path coverage."""

    def test_2_1_command_smoke(self, master_board, slave_board):
        """Cover status/config, transceive and slave commands."""
        assert "State: ready" in master_board.send_cmd("qcom spi status")
        assert "State: ready" in slave_board.send_cmd("qcom spi status")

        slave_board.send_cmd("qcom spi config -r slave -m 0 -f 1000000")
        master_board.send_cmd("qcom spi config -r master -m 0 -f 1000000")

        # Exercise SPI_HOLD_ON_CS and the matching release command.  Do not
        # start an asynchronous slave transfer here: with CS held, that
        # transfer intentionally remains active and would block the session
        # UART before the next test's baseline configuration.  Dedicated STO
        # tests below cover the master/slave held-CS behavior.
        resp = master_board.send_cmd("qcom spi transceive 5A5A5A5A -c")
        if "supported in SPI interrupt mode only" in resp:
            assert "ERROR: -c is supported in SPI interrupt mode only" in resp
            return
        assert "OK:" in resp
        assert "OK: SPI released" in master_board.send_cmd("qcom spi release")

    def test_2_2_full_duplex_4B(self, master_board, slave_board):
        slave_board.send_cmd("qcom spi config -r slave -m 0 -f 1000000")
        master_board.send_cmd("qcom spi config -r master -m 0 -f 1000000")
        slave_board.send_cmd_async("qcom spi slave_prepare CCDD1122")
        time.sleep(SLAVE_READY_DELAY_S)
        resp = master_board.send_cmd("qcom spi transceive AABB3344")
        assert extract_rx(resp) == "CCDD1122"
        assert extract_data(slave_board.send_cmd("qcom spi slave_read -t 3000")) == "AABB3344"

    def test_2_3_full_duplex_64B(self, master_board, slave_board):
        slave_board.send_cmd("qcom spi config -r slave -m 0 -f 1000000")
        master_board.send_cmd("qcom spi config -r master -m 0 -f 1000000")
        slave_board.send_cmd_async("qcom spi slave_prepare -p fixed:0x55 -l 64")
        time.sleep(SLAVE_READY_DELAY_S)
        resp = master_board.send_cmd("qcom spi transceive -p incr -l 64", timeout=10)
        assert "OK:" in resp
        assert extract_rx(resp) == "55" * 64
        data = extract_data(slave_board.send_cmd("qcom spi slave_read -t 3000"))
        assert data is not None and data.startswith("00010203")

    @pytest.mark.parametrize("mode", [0, 1, 2, 3],
                             ids=["Mode0", "Mode1", "Mode2", "Mode3"])
    def test_2_4_dual_board_spi_mode(self, master_board, slave_board, mode):
        slave_board.send_cmd(f"qcom spi config -r slave -m {mode} -f 1000000")
        master_board.send_cmd(f"qcom spi config -r master -m {mode} -f 1000000")
        slave_board.send_cmd_async("qcom spi slave_prepare A5A5A5A5")
        time.sleep(SLAVE_READY_DELAY_S)
        resp = master_board.send_cmd("qcom spi transceive 5A5A5A5A")
        assert extract_rx(resp) == "A5A5A5A5"
        assert extract_data(slave_board.send_cmd("qcom spi slave_read -t 3000")) == "5A5A5A5A"

    @pytest.mark.parametrize("freq", [500000, 1000000, 40000000],
                             ids=["500kHz", "1MHz", "40MHz"])
    def test_2_5_dual_board_representative_frequency(self, master_board, slave_board, freq):
        slave_board.send_cmd(f"qcom spi config -r slave -m 0 -f {freq}")
        master_board.send_cmd(f"qcom spi config -r master -m 0 -f {freq}")
        slave_board.send_cmd_async("qcom spi slave_prepare 12345678")
        time.sleep(SLAVE_READY_DELAY_S)
        resp = master_board.send_cmd("qcom spi transceive DEADBEEF")
        assert extract_rx(resp) == "12345678"
        assert extract_data(slave_board.send_cmd("qcom spi slave_read -t 3000")) == "DEADBEEF"

    def test_2_6_repeated_exchanges(self, master_board, slave_board):
        """Keep repeated exchange coverage without making L4 a stress test."""
        slave_board.send_cmd("qcom spi config -r slave -m 0 -f 2000000")
        master_board.send_cmd("qcom spi config -r master -m 0 -f 2000000")
        for i in range(3):
            tx_data = f"{i:02X}" * 4
            slave_tx = f"{(0xFF - i):02X}" * 4
            slave_board.send_cmd_async(f"qcom spi slave_prepare {slave_tx}")
            time.sleep(SLAVE_READY_DELAY_S)
            resp = master_board.send_cmd(f"qcom spi transceive {tx_data}")
            assert extract_rx(resp) == slave_tx
            assert extract_data(slave_board.send_cmd("qcom spi slave_read -t 3000")) == tx_data

    @pytest.mark.parametrize("word_size,tx,slave_tx", [
        (8, "AABBCCDD", "11223344"),
        (16, "AABBCCDD", "11223344"),
        (24, "AABBCCDDEEFF", "112233445566"),
        (32, "AABBCCDD", "11223344"),
    ], ids=["8bit", "16bit", "24bit", "32bit"])
    def test_2_7_dual_board_word_size(self, master_board, slave_board,
                                       word_size, tx, slave_tx):
        cfg = (f"-m 0 -f {WORD_SIZE_DIAGNOSTIC_FREQUENCY_HZ} -w {word_size}")
        slave_board.send_cmd(f"qcom spi config -r slave {cfg}")
        master_board.send_cmd(f"qcom spi config -r master {cfg}")
        slave_board.send_cmd_async(f"qcom spi slave_prepare {slave_tx}")
        time.sleep(SLAVE_READY_DELAY_S)
        resp = master_board.send_cmd(f"qcom spi transceive {tx}")
        assert extract_rx(resp) == slave_tx
        assert extract_data(slave_board.send_cmd("qcom spi slave_read -t 3000")) == tx

    def test_2_9_dual_board_lsb_first(self, master_board, slave_board):
        cfg = "-m 0 -f 1000000 -o lsb"
        slave_board.send_cmd(f"qcom spi config -r slave {cfg}")
        master_board.send_cmd(f"qcom spi config -r master {cfg}")
        slave_board.send_cmd_async("qcom spi slave_prepare 11223344")
        time.sleep(SLAVE_READY_DELAY_S)
        resp = master_board.send_cmd("qcom spi transceive AABBCCDD")
        assert extract_rx(resp) == "11223344"
        assert extract_data(slave_board.send_cmd("qcom spi slave_read -t 3000")) == "AABBCCDD"

    @pytest.mark.interrupt_only
    def test_2_8_slave_timeout(self, master_board, slave_board):
        """Retain the interrupt-only timeout behavior check."""
        slave_board.send_cmd("qcom spi config -r slave -m 0 -f 1000000")
        master_board.send_cmd("qcom spi config -r master -m 0 -f 1000000")
        slave_board.send_cmd_async("qcom spi slave_prepare -p fixed:0xDE -l 64")
        time.sleep(SLAVE_READY_DELAY_S)

        resp = master_board.send_cmd("qcom spi transceive AABBCCDD -c", timeout=5)
        assert "OK:" in resp
        resp = slave_board.send_cmd("qcom spi slave_read -t 5000", timeout=8)
        assert "ERROR:" in resp
        assert "hardware timeout" in resp.lower()
        assert "OK: SPI released" in master_board.send_cmd("qcom spi release")

    def test_2_10_invalid_arguments(self, master_board, slave_board):
        """Cover representative rejected frequency and word-size arguments."""
        for freq in (0, 40_000_001):
            slave_resp = slave_board.send_cmd(
                f"qcom spi config -r slave -m 0 -f {freq}")
            master_resp = master_board.send_cmd(
                f"qcom spi config -r master -m 0 -f {freq}")
            assert "ERROR: invalid frequency" in slave_resp
            assert "ERROR: invalid frequency" in master_resp

        assert "ERROR: invalid word size 12" in master_board.send_cmd(
            "qcom spi config -r master -m 0 -f 1000000 -w 12")
        assert "ERROR: invalid word size 12" in slave_board.send_cmd(
            "qcom spi config -r slave -m 0 -f 1000000 -w 12")

    @pytest.mark.dma_only
    def test_2_11_board_side_throughput_64k(self, master_board, slave_board):
        """Measure MOSI throughput at the supported 40 MHz SCLK setting."""
        slave_board.send_cmd("qcom spi config -r slave -m 0 -f 40000000")
        master_board.send_cmd("qcom spi config -r master -m 0 -f 40000000")

        slave_board.send_cmd_async(
            f"qcom spi slave_prepare -p fixed:0xA5 -l {THROUGHPUT_BLOCK_LEN} "
            f"-n {THROUGHPUT_BLOCKS}")
        time.sleep(SLAVE_READY_DELAY_S)

        master_resp, elapsed_s = master_board.send_cmd_timed(
            f"qcom spi transceive -p fixed:0xA5 -l {THROUGHPUT_BLOCK_LEN} "
            f"-n {THROUGHPUT_BLOCKS}", timeout=120)
        assert "OK: transceived" in master_resp, master_resp

        rate_mbps = THROUGHPUT_BYTES * 8 / elapsed_s / 1_000_000
        print(f"MOSI 40 MHz measured throughput: {rate_mbps:.3f} Mbps "
              f"({THROUGHPUT_BYTES} bytes in {elapsed_s * 1000:.3f} ms, "
              "host-observed reference)")
        slave_resp = slave_board.send_cmd("qcom spi slave_read", timeout=60)
        assert f"slave received {THROUGHPUT_BYTES} bytes" in slave_resp, slave_resp

    @pytest.mark.dma_only
    def test_2_12_board_side_throughput_miso_26mhz(self, master_board,
                                                    slave_board):
        """Measure MISO throughput at the stable 26 MHz read limit."""
        slave_board.send_cmd("qcom spi config -r slave -m 0 -f 26000000")
        master_board.send_cmd("qcom spi config -r master -m 0 -f 26000000")

        slave_board.send_cmd_async(
            f"qcom spi slave_prepare -p fixed:0xA5 -l {THROUGHPUT_BLOCK_LEN} "
            f"-n {THROUGHPUT_BLOCKS}")
        time.sleep(SLAVE_READY_DELAY_S)

        master_resp, elapsed_s = master_board.send_cmd_timed(
            f"qcom spi transceive -p fixed:0xA5 -l {THROUGHPUT_BLOCK_LEN} "
            f"-n {THROUGHPUT_BLOCKS}", timeout=120)
        assert "OK: transceived" in master_resp, master_resp

        rate_mbps = THROUGHPUT_BYTES * 8 / elapsed_s / 1_000_000
        print(f"MISO 26 MHz measured throughput: {rate_mbps:.3f} Mbps "
              f"({THROUGHPUT_BYTES} bytes in {elapsed_s * 1000:.3f} ms, "
              "host-observed reference)")
        slave_resp = slave_board.send_cmd("qcom spi slave_read", timeout=60)
        assert f"slave received {THROUGHPUT_BYTES} bytes" in slave_resp, slave_resp


@pytest.mark.dual_board
@pytest.mark.dma_only
class TestMultiFrameReArm:
    """Multi-block continuous transfers at a low SCLK.

    Repeated transfers are issued as extra blocks of a single transaction, so
    the link never returns to idle mid-run and the slave's DMA chain is built
    before the first CS assertion. That removes the frame boundary the earlier
    frame-per-transceive shape had, where the master re-asserted CS as soon as
    its own loop came around while the slave was still tearing down and
    rebuilding its DMA. This case runs the same multi-block shape at 2 MHz to
    keep a slow-SCLK divider on the same path.
    """

    def test_2_13_multi_frame_at_2mhz(self, master_board, slave_board):
        slave_board.send_cmd("qcom spi config -r slave -m 0 -f 2000000")
        master_board.send_cmd("qcom spi config -r master -m 0 -f 2000000")

        slave_board.send_cmd_async(
            "qcom spi slave_prepare -p fixed:0xA5 -l 65536 -n 4")
        time.sleep(SLAVE_READY_DELAY_S)

        master_resp = master_board.send_cmd(
            "qcom spi transceive -p incr -l 65536 -n 4", timeout=180)
        assert "OK: transceived" in master_resp, master_resp

        slave_resp = slave_board.send_cmd("qcom spi slave_read", timeout=120)
        assert "OK: slave received" in slave_resp, slave_resp
        assert "slave received 262144 bytes" in slave_resp, slave_resp


@pytest.mark.dual_board
@pytest.mark.dma_only
@pytest.mark.dma_read_direction
class TestDmaReadDirection:
    """Check the DMA *read* direction (slave -> master over MISO).

    ``dual_board.dma_throughput`` runs a real 40 MHz SCLK but only proves the
    write direction: the slave verifies the payload it received, and nothing
    looks at the master's RX buffer. SPI is full duplex, so the existing DMA
    transceive already carries slave data back on the same clocks. This case
    therefore has the slave present a fixed non-repeating pattern, has the
    master transceive its own pattern, and checks *both* buffers.

    The supported MISO ceiling is checked repeatedly. The known-marginal 40 MHz
    return path remains covered by the read-margin diagnostic and throughput
    sweep, but is not a required pass point here.
    """

    @pytest.mark.parametrize("freq", [
        READ_DIRECTION_CONTROL_FREQUENCY_HZ,
        READ_DIRECTION_SUPPORTED_FREQUENCY_HZ,
    ], ids=["1MHz", "26MHz"])
    def test_2_14_dma_read_direction(self, master_board, slave_board, freq):
        slave_resp = slave_board.send_cmd(f"qcom spi config -r slave -m 0 -f {freq}")
        assert "OK:" in slave_resp, slave_resp
        master_resp = master_board.send_cmd(f"qcom spi config -r master -m 0 -f {freq}")
        assert "OK:" in master_resp, master_resp

        master_rx_samples = []
        slave_rx_samples = []

        for _ in range(READ_DIRECTION_ITERATIONS):
            slave_board.send_cmd_async(
                f"qcom spi slave_prepare {READ_DIRECTION_SLAVE_TX}")
            time.sleep(SLAVE_READY_DELAY_S)
            resp = master_board.send_cmd(
                f"qcom spi transceive {READ_DIRECTION_MASTER_TX}")
            master_rx_samples.append(extract_rx(resp))
            slave_rx_samples.append(
                extract_data(slave_board.send_cmd("qcom spi slave_read -t 3000")))

        report = _format_read_direction_report(
            freq, master_rx_samples, slave_rx_samples)
        # Print as well as log: a passing pytest case swallows the log handler
        # output in some twister configurations, and this distribution is the
        # actual result of the case even when it passes.
        print(report)
        _log.info(report)

        master_exact = sum(1 for rx in master_rx_samples
                           if rx == READ_DIRECTION_SLAVE_TX)
        slave_exact = sum(1 for rx in slave_rx_samples
                          if rx == READ_DIRECTION_MASTER_TX)

        # The write direction is the established baseline; report it separately
        # from the read direction so a read failure is not misread as a link
        # or wiring problem.
        assert slave_exact == READ_DIRECTION_ITERATIONS, (
            f"write direction (MOSI) failed at {freq} Hz: "
            f"{slave_exact}/{READ_DIRECTION_ITERATIONS} exact\n{report}")
        assert master_exact == READ_DIRECTION_ITERATIONS, (
            f"read direction (MISO) failed at {freq} Hz: "
            f"{master_exact}/{READ_DIRECTION_ITERATIONS} exact\n{report}")


def _hamming_distance(observed_hex, expected_hex):
    """Bit distance between two equal-length hex payloads, else None."""
    if observed_hex is None:
        return None
    try:
        observed = bytes.fromhex(observed_hex)
        expected = bytes.fromhex(expected_hex)
    except ValueError:
        return None
    if len(observed) != len(expected):
        return None
    return sum(bin(o ^ e).count("1") for o, e in zip(observed, expected))


def _xor_hex(observed_hex, expected_hex):
    """XOR of two equal-length hex payloads as hex, else None."""
    if observed_hex is None:
        return None
    try:
        observed = bytes.fromhex(observed_hex)
        expected = bytes.fromhex(expected_hex)
    except ValueError:
        return None
    if len(observed) != len(expected):
        return None
    return bytes(o ^ e for o, e in zip(observed, expected)).hex().upper()


def _format_read_direction_report(freq, master_rx_samples, slave_rx_samples):
    """Build the per-frequency distribution report for the read-direction case."""
    total = len(master_rx_samples)
    master_exact = sum(1 for rx in master_rx_samples
                       if rx == READ_DIRECTION_SLAVE_TX)
    slave_exact = sum(1 for rx in slave_rx_samples
                      if rx == READ_DIRECTION_MASTER_TX)

    lines = [
        "",
        "=" * 70,
        f"DMA read-direction result @ {freq / 1_000_000:g} MHz requested SCLK",
        "=" * 70,
        f"  slave TX (expected master RX): {READ_DIRECTION_SLAVE_TX}",
        f"  master TX (expected slave RX): {READ_DIRECTION_MASTER_TX}",
        f"  samples                      : {total}",
        f"  master RX exact (MISO/read)  : {master_exact}"
        f"  errors: {total - master_exact}",
        f"  slave  RX exact (MOSI/write) : {slave_exact}"
        f"  errors: {total - slave_exact}",
        "",
        "  master RX distribution (observed x count, XOR vs expected, hamming):",
    ]

    for value, count in Counter(master_rx_samples).most_common():
        shown = value if value is not None else "<no RX line>"
        xor = _xor_hex(value, READ_DIRECTION_SLAVE_TX)
        distance = _hamming_distance(value, READ_DIRECTION_SLAVE_TX)
        detail = "" if value == READ_DIRECTION_SLAVE_TX else (
            f"   xor={xor if xor is not None else 'n/a'}"
            f" hamming={distance if distance is not None else 'n/a'}")
        lines.append(f"    {shown:<24} x {count:<3}{detail}")

    lines.append("")
    lines.append("  slave RX distribution (observed x count):")
    for value, count in Counter(slave_rx_samples).most_common():
        shown = value if value is not None else "<no DATA line>"
        lines.append(f"    {shown:<24} x {count}")

    lines.append("")
    lines.append("  per-sample master RX sequence:")
    for index, value in enumerate(master_rx_samples):
        mark = "ok " if value == READ_DIRECTION_SLAVE_TX else "BAD"
        shown = value if value is not None else "<no RX line>"
        lines.append(f"    [{index:02d}] {mark} {shown}")

    lines.append("=" * 70)
    return "\n".join(lines)



def _to_bits(payload):
    """MSB-first bit list for a bytes payload."""
    return [(byte >> (7 - offset)) & 1
            for byte in payload for offset in range(8)]


def _classify_bit_errors(observed_hex, expected_hex):
    """Compare one sample bit by bit against the late-sampling model.

    The model under test is that the master's capture of MISO lands so close to
    the point where the slave changes the line that a bit can resolve to either
    its own value or the value still being driven from the bit before it. A
    wrong bit is therefore only *explainable* where the slave's stream changes
    value; at a boundary where the stream holds, the model forbids an error.

    Returns a dict of counts, or None when the sample is unusable.
    """
    if observed_hex is None:
        return None
    try:
        observed = _to_bits(bytes.fromhex(observed_hex))
        expected = _to_bits(bytes.fromhex(expected_hex))
    except ValueError:
        return None
    if len(observed) != len(expected):
        return None

    # Bit -1 is the idle line level before the slave drives its first bit.
    previous = [0] + expected[:-1]
    transitions = [i for i in range(len(expected)) if previous[i] != expected[i]]
    wrong = [i for i in range(len(expected)) if observed[i] != expected[i]]

    return {
        "bits": len(expected),
        "transitions": len(transitions),
        "wrong": wrong,
        # Held-value positions: an error here would contradict the model.
        "wrong_on_hold": [i for i in wrong if previous[i] == expected[i]],
        "all_slipped": observed == previous,
    }


@pytest.mark.dual_board
@pytest.mark.dma_only
@pytest.mark.dma_read_direction
@pytest.mark.dma_read_margin
class TestDmaReadMargin:
    """Locate *why* the 40 MHz DMA read direction fails.

    ``dma_read_direction`` establishes that it fails; 4 bytes cannot say where
    the fault is. This case exchanges 64 bytes so each sample carries 512 bits,
    and sweeps the SCLK steps the 160 MHz source can reach. It separates three
    candidate explanations:

    * slave armed late / the two boards need to wait for each other — then wrong
      bits cluster at the start of the payload and stop once the slave catches
      up;
    * random corruption or bad wiring — then wrong bits ignore the content of
      the slave's stream and appear at held-value positions too;
    * the master samples MISO too late in the bit period — then every wrong bit
      sits at a position where the slave's stream *changed*, and carries the
      previous bit's value, spread over the whole payload.

    The bit-error rate against SCLK also bounds the round-trip delay, since the
    sampling window is half an SCLK period.
    """

    @pytest.mark.parametrize("freq", READ_MARGIN_FREQUENCIES_HZ,
                             ids=[f"{f // 1_000_000}MHz"
                                  for f in READ_MARGIN_FREQUENCIES_HZ])
    def test_2_15_dma_read_margin(self, master_board, slave_board, freq):
        length = len(READ_MARGIN_SLAVE_TX) // 2
        assert "OK:" in master_board.send_cmd(
            f"qcom spi config -r master -m 0 -f {freq}")
        assert "OK:" in slave_board.send_cmd(
            f"qcom spi config -r slave -m 0 -f {freq}")

        samples = []
        for _ in range(READ_MARGIN_ITERATIONS):
            slave_board.send_cmd_async(
                f"qcom spi slave_prepare {READ_MARGIN_SLAVE_TX}")
            time.sleep(SLAVE_READY_DELAY_S)
            resp = master_board.send_cmd(
                f"qcom spi transceive -p incr -l {length}", timeout=10)
            samples.append(extract_rx(resp))
            # Drain the slave transfer so the next iteration re-arms cleanly.
            slave_board.send_cmd("qcom spi slave_read -t 3000")

        report, totals = _format_read_margin_report(freq, samples)
        print(report)
        _log.info(report)

        # Only the model check is asserted. The bit-error rate itself is the
        # measurement this case exists to produce, and it is expected to be
        # non-zero at the top of the range.
        assert totals["unusable"] == 0, report
        assert totals["wrong_on_hold"] == 0, (
            f"{totals['wrong_on_hold']} wrong bits landed where the slave's "
            f"stream held its value, which late sampling cannot explain\n"
            f"{report}")


def _format_read_margin_report(freq, samples):
    """Aggregate the read-margin samples into a report and totals."""
    totals = {"samples": len(samples), "unusable": 0, "exact": 0, "bits": 0,
              "wrong": 0, "wrong_on_hold": 0, "all_slipped": 0}
    first_error_positions = []
    error_positions = []

    for sample in samples:
        if sample == READ_MARGIN_SLAVE_TX:
            totals["exact"] += 1
        info = _classify_bit_errors(sample, READ_MARGIN_SLAVE_TX)
        if info is None:
            totals["unusable"] += 1
            continue
        totals["bits"] += info["bits"]
        totals["wrong"] += len(info["wrong"])
        totals["wrong_on_hold"] += len(info["wrong_on_hold"])
        totals["all_slipped"] += 1 if info["all_slipped"] else 0
        if info["wrong"]:
            first_error_positions.append(info["wrong"][0])
            error_positions.extend(info["wrong"])

    ber = (totals["wrong"] / totals["bits"]) if totals["bits"] else 0.0
    # Half an SCLK period is the window from the slave's launch edge to the
    # master's sampling edge in mode 0.
    half_period_ns = 1e9 / freq / 2

    lines = [
        "",
        "=" * 70,
        f"DMA read-margin diagnostic @ {freq / 1_000_000:g} MHz requested SCLK"
        f"  (sampling window ~{half_period_ns:.2f} ns)",
        "=" * 70,
        f"  exchanges                    : {totals['samples']}"
        f"  ({len(READ_MARGIN_SLAVE_TX) // 2} bytes each)",
        f"  exchanges fully correct      : {totals['exact']}",
        f"  unusable / no RX line        : {totals['unusable']}",
        f"  bits compared                : {totals['bits']}",
        f"  bits wrong                   : {totals['wrong']}"
        f"  (bit error rate {ber:.2e})",
        f"  ...of those at a position where the slave's stream CHANGED:"
        f" {totals['wrong'] - totals['wrong_on_hold']}",
        f"  ...of those at a position where the slave's stream HELD:"
        f" {totals['wrong_on_hold']}   <- must be 0 for late sampling",
        f"  exchanges shifted right by exactly one bit: {totals['all_slipped']}",
    ]

    if error_positions:
        span = f"{min(error_positions)}..{max(error_positions)}"
        lines.append(f"  wrong-bit positions span     : {span} of "
                     f"{len(READ_MARGIN_SLAVE_TX) * 4} bits")
        lines.append(f"  first wrong bit per bad exchange: "
                     f"{sorted(first_error_positions)}")
        # A startup/readiness fault can only corrupt the beginning; count how
        # many wrong bits land in the second half of the payload.
        half = len(READ_MARGIN_SLAVE_TX) * 4 // 2
        late = sum(1 for position in error_positions if position >= half)
        lines.append(f"  wrong bits in the SECOND half of the payload: {late}"
                     f"   <- non-zero rules out a slave-arming/wait problem")
    else:
        lines.append("  no wrong bits at this frequency")

    lines.append("=" * 70)
    return "\n".join(lines), totals


@pytest.mark.dual_board
@pytest.mark.dma_only
@pytest.mark.dma_sweep
class TestDmaThroughputSweep:
    """Board-measured throughput at several SCLK steps, both directions at once.

    ``test_2_11_board_side_throughput_64k`` pins a single frequency and only
    proves the write direction. This sweeps the SCLK steps of interest and checks
    both directions of the same transfers: the slave verifies the incrementing
    pattern it received (write/MOSI) while the master verifies the fixed byte the
    slave streamed back (read/MISO).

    Rates come from the elapsed time each board measured itself; nothing here is
    derived from the requested frequency. The read direction is allowed to fail
    at 40 MHz because that limitation is measured and documented rather than
    fixed - see md/spi/40mhz-read-failure.md.
    """

    @pytest.mark.parametrize("freq", SWEEP_FREQUENCIES_HZ,
                             ids=[f"{f // 1_000_000}MHz"
                                  for f in SWEEP_FREQUENCIES_HZ])
    def test_2_16_throughput_sweep_both_directions(self, master_board,
                                                   slave_board, freq):
        slave_resp = slave_board.send_cmd(f"qcom spi config -r slave -m 0 -f {freq}")
        assert "OK:" in slave_resp, slave_resp
        master_resp = master_board.send_cmd(f"qcom spi config -r master -m 0 -f {freq}")
        assert "OK:" in master_resp, master_resp

        slave_board.send_cmd_async(
            f"qcom spi slave_prepare -p fixed:0x{SWEEP_SLAVE_TX_BYTE:02X} "
            f"-l {SWEEP_PAYLOAD_LEN} "
            f"-n {SWEEP_BLOCKS}")
        time.sleep(SLAVE_READY_DELAY_S)

        start_time = time.monotonic()
        master_resp = master_board.send_cmd(
            f"qcom spi transceive -p incr -l {SWEEP_PAYLOAD_LEN} -n {SWEEP_BLOCKS}",
            timeout=180)
        elapsed_ms = max(1, int((time.monotonic() - start_time) * 1000))
        assert "OK: transceived" in master_resp, master_resp

        slave_resp = slave_board.send_cmd("qcom spi slave_read", timeout=120)
        assert "OK: slave received" in slave_resp, slave_resp

        report, result = _format_sweep_report(freq, master_resp, slave_resp,
                                              elapsed_ms)
        print(f"\n{report}")
        _log.info("\n%s", report)

        expected_bytes = SWEEP_PAYLOAD_LEN * SWEEP_BLOCKS
        assert result["slave_bytes"] == expected_bytes, report
        # Write direction must be clean at every step, including 40 MHz.
        assert result["write_errors"] == 0, (
            f"write direction (MOSI) corrupted at {freq // 1_000_000} MHz\n{report}")

        if freq in SWEEP_READ_TOLERATED_HZ:
            # Recorded, not asserted. Failing here would only restate a known
            # and documented hardware limit.
            if result["read_errors"]:
                _log.warning(
                    "read direction (MISO) failed at %d MHz as documented: "
                    "%d of %d bytes wrong",
                    freq // 1_000_000, result["read_errors"],
                    result["read_checked"])
        else:
            assert result["read_errors"] == 0, (
                f"read direction (MISO) corrupted at {freq // 1_000_000} MHz, "
                f"which is below the documented 26.67 MHz ceiling\n{report}")


def _search_int(pattern, text, default=None):
    """Pull a single integer field out of a shell response line."""
    match = re.search(pattern, text)
    return default if match is None else int(match.group(1))


def _format_sweep_report(freq, master_resp, slave_resp, elapsed_ms):
    """Build the per-frequency sweep report and the values it asserts on."""
    result = {
        "rate_mbps": None,
        "elapsed_ms": elapsed_ms,
        "read_errors": 0,
        "read_checked": 0,
        "first_bad": -1,
        "write_errors": 0,
        "slave_bytes": _search_int(r"slave received (\d+) bytes", slave_resp),
    }

    result["rate_mbps"] = (
        SWEEP_PAYLOAD_LEN * SWEEP_BLOCKS * 8.0
        / (result["elapsed_ms"] * 1000.0)
    )

    read_note = ""
    if result["read_errors"]:
        ratio = result["read_errors"] / result["read_checked"]
        read_note = (f"   byte error rate {ratio:.2e}, "
                     f"first bad byte at offset {result['first_bad']}")

    lines = [
        "=" * 70,
        f"Throughput sweep @ {freq // 1_000_000} MHz requested SCLK",
        "=" * 70,
        f"  payload                      : {SWEEP_BLOCKS} x {SWEEP_PAYLOAD_LEN} "
        f"bytes in one transaction",
        f"  board-measured elapsed       : {result['elapsed_ms']} ms",
        f"  board-measured rate          : {result['rate_mbps']} Mbps",
        f"  write (MOSI) errors on slave : {result['write_errors']}"
        f"   bytes accounted: {result['slave_bytes']}",
        f"  read  (MISO) errors on master: {result['read_errors']} of "
        f"{result['read_checked']} checked{read_note}",
        "=" * 70,
    ]
    return "\n".join(lines), result
