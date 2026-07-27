#!/usr/bin/env python3

# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

QCC74X_BLOB_BASE = ROOT / "modules" / "hal" / "qcom" / "qcc74x" / "zephyr" / "blobs" / "qcc74x"
QCC74X_BIN_BASE = ROOT / "prop" / "qcc74x-bin"


def copy_tree(src: Path, dst: Path) -> None:
    if not src.exists():
        print(f"Source not found, skipping: {src}", file=sys.stderr)
        return
    shutil.copytree(src, dst, dirs_exist_ok=True)


def main() -> int:
    if not QCC74X_BIN_BASE.exists():
        print(f"QCC74X_BIN_BASE not found: {QCC74X_BIN_BASE}", file=sys.stderr)
        return 1

    QCC74X_BLOB_BASE.mkdir(parents=True, exist_ok=True)

    for name in ("bin", "lib", "tool"):
        copy_tree(QCC74X_BIN_BASE / name, QCC74X_BLOB_BASE / name)

    return 0


if __name__ == "__main__":
    sys.exit(main())
