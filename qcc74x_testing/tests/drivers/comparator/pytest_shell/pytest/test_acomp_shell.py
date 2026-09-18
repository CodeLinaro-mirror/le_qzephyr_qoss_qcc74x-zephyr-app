# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

"""Non-PM L4 reference test for the QCC74x ``qcom acomp`` shell commands.

This test is executed against the Wi-Fi shell sample. It intentionally avoids
``await`` and long-delay ``loop`` commands because those cases are reserved for
separate PDS15/PM validation.

Example device run from the West workspace root::

    west twister -T qapp/qcc74x/qcc74x_testing/tests/drivers/comparator/pytest_shell \
      -p qcc748m_evk \
      --device-testing --flash-before \
      --hardware-map $env:USERPROFILE/.zephyr-twister/hardware-map.yaml \
      --inline-logs -vvv --clobber-output
"""

import logging

from twister_harness import DeviceAdapter, Shell

LOGGER = logging.getLogger(__name__)


def _exec(shell: Shell, command: str, expected: str) -> str:
    """Run a synchronous shell command and assert one expected response."""
    assert shell.wait_for_prompt(), "shell prompt was not received"
    output = "\n".join(shell.exec_command(command, timeout=10.0))
    assert expected in output, f"{command!r}: expected {expected!r}, got:\n{output}"
    return output


def _exec_async(dut: DeviceAdapter, shell: Shell, command: str, expected: str) -> str:
    """Wait for a service-thread result that can follow an early shell prompt."""
    dut.clear_buffer()
    dut.write(f"{command}\n".encode())
    lines = dut.readlines_until(regex=expected, timeout=20.0, print_output=True)
    output = "\n".join(lines)
    assert expected in output, f"{command!r}: expected {expected!r}, got:\n{output}"
    assert shell.wait_for_prompt(), "shell prompt did not return after async command"
    return output


def test_qcom_acomp_l4_non_pm(shell: Shell, dut: DeviceAdapter):
    """Exercise device selection, edge semantics, shared IRQ and selftest."""
    LOGGER.info("verify qcom root and two ready comparator devices")
    _exec(shell, "qcom acomp list", "acomp devices: 2")
    _exec(shell, "qcom acomp status 0", "ready=1")
    _exec(shell, "qcom acomp status 1", "ready=1")

    LOGGER.info("verify rising and falling pending semantics on comparator0")
    _exec(shell, "qcom acomp trigger 0 rising", "trigger set: rising")
    _exec(shell, "qcom acomp callback 0 off", "callback off")
    _exec(shell, "qcom acomp drive 0", "drive gpio -> 0")
    _exec(shell, "qcom acomp pending 0", "pending(read+clear): 0")
    _exec(shell, "qcom acomp drive 1", "drive gpio -> 1")
    _exec(shell, "qcom acomp pending 0", "pending(read+clear): 1")
    _exec(shell, "qcom acomp trigger 0 falling", "trigger set: falling")
    _exec(shell, "qcom acomp drive 0", "drive gpio -> 0")
    _exec(shell, "qcom acomp pending 0", "pending(read+clear): 1")
    _exec(shell, "qcom acomp drive 1", "drive gpio -> 1")
    _exec(shell, "qcom acomp pending 0", "pending(read+clear): 0")

    LOGGER.info("verify callback consumes the event while pending mode preserves it")
    _exec(shell, "qcom acomp trigger 0 both", "trigger set: both")
    _exec(shell, "qcom acomp callback 0 off", "callback off")
    _exec(shell, "qcom acomp drive 0", "drive gpio -> 0")
    _exec(shell, "qcom acomp drive 1", "drive gpio -> 1")
    _exec(shell, "qcom acomp pending 0", "pending(read+clear): 1")
    _exec(shell, "qcom acomp callback 0 on", "callback on")
    _exec(shell, "qcom acomp drive 0", "drive gpio -> 0")
    _exec(shell, "qcom acomp drive 1", "drive gpio -> 1")
    _exec(shell, "qcom acomp pending 0", "pending(read+clear): 0")

    LOGGER.info("verify both comparator instances receive the shared GPIO20 edge")
    _exec(shell, "qcom acomp trigger 0 both", "trigger set: both")
    _exec(shell, "qcom acomp trigger 1 both", "trigger set: both")
    _exec(shell, "qcom acomp callback 0 off", "callback off")
    _exec(shell, "qcom acomp callback 1 off", "callback off")
    _exec(shell, "qcom acomp drive 0", "drive gpio -> 0")
    _exec(shell, "qcom acomp drive 1", "drive gpio -> 1")
    _exec(shell, "qcom acomp pending 0", "pending(read+clear): 1")
    _exec(shell, "qcom acomp pending 1", "pending(read+clear): 1")
    _exec(shell, "qcom acomp output 0", "output: 1")
    _exec(shell, "qcom acomp output 1", "output: 1")

    LOGGER.info("verify asynchronous service-thread selftests")
    _exec_async(dut, shell, "qcom acomp selftest 0 500", "selftest: RESULT PASS")
    _exec_async(dut, shell, "qcom acomp selftest 1 500", "selftest: RESULT PASS")
