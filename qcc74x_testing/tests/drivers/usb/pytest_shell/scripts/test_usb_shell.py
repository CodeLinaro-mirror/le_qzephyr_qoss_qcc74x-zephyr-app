# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

def send(shell, command):
    output = shell.exec_command(command)
    return "\n".join(output)


def test_usb_shell_lifecycle(shell):
    for command in ("usbd defcfg", "usbd enable", "usbd disable", "usbd shutdown"):
        output = send(shell, command)
        assert "Error" not in output
        assert "error" not in output


def test_loopback_shell_commands(shell):
    output = send(shell, "lb bulk manual on loopback_0")
    assert "Error" not in output
