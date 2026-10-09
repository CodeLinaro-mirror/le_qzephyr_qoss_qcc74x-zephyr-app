# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

import pytest


@pytest.mark.parametrize("payload", [
    b"usb-l0\n",
    bytes(range(64)),
    bytes((i * 7) & 0xFF for i in range(256)),
    bytes((i * 13) & 0xFF for i in range(1024)),
])
def test_cdc_acm_echo(usb, payload):
    received = usb.exchange(payload)
    assert received == payload
