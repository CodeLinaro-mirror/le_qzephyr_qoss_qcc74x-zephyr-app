# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/usr/bin/env python3
"""HW test bench health check - verify boards are alive before running tests."""

import argparse
import errno
import os
import platform
import subprocess
import sys
import time
from pathlib import Path

import usb.core
import yaml

# =============================================================================
# Constants
# =============================================================================

SCRIPT_DIR = Path(__file__).resolve().parent
UTILS_DIR = SCRIPT_DIR.parent
WORKSPACE_ROOT = UTILS_DIR.parent
REGRESSION_SCRIPT = UTILS_DIR / "regression" / "run_regression.py"
DEFAULT_HARDWARE_MAP = Path.home() / ".zephyr-twister" / "hardware-map.yaml"
CI_HARDWARE_MAP = SCRIPT_DIR / "ci-hardware-map.yaml"

SERIAL_RECOVERY_TIMEOUT_S = 10
SERIAL_STABLE_WINDOW_S = 7

# Keep platform handling aligned with run_regression.py.
DEVICE_PLATFORMS = frozenset({"qcc743m_evk", "qcc744m_evk", "qcc748m_evk"})
BUILD_ONLY_PLATFORMS = frozenset({"qcc75xm_evk"})
KNOWN_PLATFORMS = DEVICE_PLATFORMS | BUILD_ONLY_PLATFORMS

EXIT_OK = 0
EXIT_FAILURE = 1

# =============================================================================
# Hardware map resolution
# =============================================================================

def _resolve_hardware_map() -> Path:
    """Return the hardware map path to use.

    When running on the CI machine, use hardware map from the repository.
    Otherwise fall back to the developer-local default location.
    """
    if os.environ.get("QCC_CI_MACHINE"):
        print(f"[health] CI environment detected - using CI hardware map: {CI_HARDWARE_MAP}")
        return CI_HARDWARE_MAP
    print(f"Using default hardware map: {DEFAULT_HARDWARE_MAP}")
    return DEFAULT_HARDWARE_MAP


# =============================================================================
# Logging helpers
# =============================================================================

_verbose = True

def _info(msg: str) -> None:
    print(f"[health] {msg}")


def _debug(msg: str) -> None:
    if _verbose:
        print(f"[health][debug] {msg}")


def _warn(msg: str) -> None:
    print(f"[health][WARN] {msg}")


def _error(msg: str) -> None:
    print(f"[health][ERROR] {msg}")


def _sep(title: str = "") -> None:
    line = "=" * 72
    print(line)
    if title:
        print(title)
        print(line)


def _platform_base(plat: str) -> str:
    """Strip variant qualifier from a platform name."""
    return plat.split("/")[0]


# =============================================================================
# Step 1: Workspace sanity check
# =============================================================================

def _check_workspace() -> bool:
    """Verify west workspace, zephyr dir, venv, and hardware map exist."""
    _sep("Step 1: Workspace sanity check")
    ok = True

    # west topdir
    try:
        result = subprocess.run(
            ["west", "topdir"],
            capture_output=True, text=True, timeout=10,
            cwd=str(WORKSPACE_ROOT),
        )
        if result.returncode == 0:
            topdir = result.stdout.strip()
            _info(f"West workspace: {topdir}")
        else:
            _error(f"'west topdir' failed: {result.stderr.strip()}")
            ok = False
    except FileNotFoundError:
        _error("'west' command not found - is the venv activated?")
        ok = False
    except subprocess.TimeoutExpired:
        _error("'west topdir' timed out")
        ok = False

    # zephyr/ directory
    zephyr_dir = WORKSPACE_ROOT / "zephyr"
    if zephyr_dir.is_dir():
        _info(f"Zephyr directory: {zephyr_dir}")
    else:
        _error(f"Zephyr directory not found: {zephyr_dir}")
        ok = False

    # .venv
    if platform.system() == "Windows":
        venv_python = WORKSPACE_ROOT / ".venv" / "Scripts" / "python.exe"
    else:
        venv_python = WORKSPACE_ROOT / ".venv" / "bin" / "python"
    if venv_python.exists():
        _info(f"Venv python: {venv_python}")
    else:
        _error(f"Venv python not found: {venv_python}")
        ok = False

    if ok:
        _info("Workspace sanity check: PASS")
    else:
        _error("Workspace sanity check: FAIL")
    print()
    return ok


# =============================================================================
# Hardware map parsing
# =============================================================================

def _load_hardware_map(hw_map_path: Path) -> list[dict]:
    """Parse hardware-map.yaml and return list of connected board entries."""
    if not hw_map_path.exists():
        _error(f"Hardware map not found: {hw_map_path}")
        return []

    try:
        with open(hw_map_path, encoding="utf-8") as f:
            data = yaml.safe_load(f)
    except yaml.YAMLError as exc:
        _error(f"Failed to parse hardware map: {exc}")
        return []

    if not isinstance(data, list):
        _error("Hardware map is not a YAML list")
        return []

    connected = [entry for entry in data if entry.get("connected", False)]
    _debug(f"Hardware map: {len(data)} total entries, {len(connected)} connected")
    return connected


# =============================================================================
# Port accessibility check
# =============================================================================

def _wait_port_accessible(serial_path: str, timeout_s: float = 5.0) -> bool:
    """Poll until the serial port can actually be opened (not busy)."""
    deadline = time.monotonic() + timeout_s
    while True:
        try:
            fd = os.open(serial_path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
            os.close(fd)
            return True
        except OSError as exc:
            if exc.errno in (errno.EBUSY, errno.EACCES):
                if time.monotonic() >= deadline:
                    return False
                time.sleep(0.2)
            elif exc.errno == errno.ENOENT:
                return False  # device disappeared
            else:
                _debug(f"Unexpected error opening {serial_path}: {exc}")
                return True


def _wait_port_stable(serial_path: str, timeout_s: float = 30.0) -> bool:
    """Wait until *serial_path* stays continuously accessible for SERIAL_STABLE_WINDOW_S."""
    deadline = time.monotonic() + timeout_s
    stable_until: float | None = None

    while time.monotonic() < deadline:
        try:
            fd = os.open(serial_path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
            os.close(fd)
            if stable_until is None:
                stable_until = time.monotonic() + SERIAL_STABLE_WINDOW_S
                _debug(f"{serial_path}: accessible - waiting {SERIAL_STABLE_WINDOW_S}s for stability ...")
            elif time.monotonic() >= stable_until:
                return True
        except OSError as exc:
            if exc.errno == errno.ENOENT:
                return False  # device disappeared
            # EBUSY or EACCES - something grabbed the port; reset the window
            if stable_until is not None:
                _debug(f"{serial_path}: grabbed again ({exc.strerror}), resetting stability timer")
                stable_until = None
        time.sleep(0.2)

    return False


# =============================================================================
# USB reset via PyUSB
# =============================================================================

def _find_pyusb_device(serial_path: str):
    """Return the usb.core.Device backing *serial_path*, or None."""
    # /dev/ entries are often udev symlinks (e.g. /dev/qcc744m_evk_2_1 ->
    # /dev/ttyUSB0).  /sys/class/tty/ only indexes real device names, so we
    # must resolve the symlink before building the sysfs path.
    real_path = Path(serial_path).resolve()
    if real_path != Path(serial_path):
        _debug(f"{serial_path} -> {real_path} (resolved symlink)")
    tty_name = real_path.name
    sysfs_link = Path(f"/sys/class/tty/{tty_name}/device")
    if not sysfs_link.exists():
        _debug(f"sysfs path not found: {sysfs_link}")
        return None
    try:
        usb_dev_dir = sysfs_link.resolve().parent
        busnum = int((usb_dev_dir / "busnum").read_text().strip())
        devnum = int((usb_dev_dir / "devnum").read_text().strip())
    except (OSError, ValueError) as exc:
        _debug(f"Cannot read USB bus/device numbers for {serial_path}: {exc}")
        return None

    device = usb.core.find(bus=busnum, address=devnum)
    if device is None:
        _debug(f"PyUSB could not find device (bus={busnum}, addr={devnum}) for {serial_path}")
    return device


def _usb_reconnect(serial_path: str) -> bool:
    """Reset the USB device backing *serial_path* using PyUSB / libusb."""
    device = _find_pyusb_device(serial_path)
    if device is None:
        _debug(f"USB device not visible for {serial_path} - physically disconnected?")
        return False

    _debug(f"PyUSB reset: bus={device.bus} addr={device.address} ({serial_path})")
    try:
        device.reset()
    except usb.core.USBError as exc:
        _warn(f"USB reset failed for {serial_path}: {exc}")
        return False

    # Wait for the device node to reappear after re-enumeration
    _debug(f"Waiting up to {SERIAL_RECOVERY_TIMEOUT_S}s for {serial_path} to reappear ...")
    deadline = time.monotonic() + SERIAL_RECOVERY_TIMEOUT_S
    while time.monotonic() < deadline:
        if Path(serial_path).exists():
            break
        time.sleep(0.5)
    else:
        return False  # never reappeared

    remaining = max(0.0, deadline - time.monotonic())
    if not _wait_port_accessible(serial_path, timeout_s=remaining):
        _debug(f"{serial_path} reappeared but is still busy")
        return False

    return True


# =============================================================================
# Step 2: Serial device connectivity check
# =============================================================================

def _preflight_usb_reconnect(boards: list[dict]) -> None:
    """Reconnect every board listed in the hardware map via PyUSB."""
    _sep("Step 2a: Pre-flight USB reconnect")

    reconnected = 0
    attempted = 0
    for board in boards:
        serial_path = board.get("serial", "")
        board_platform = board.get("platform", "unknown")
        if not serial_path:
            continue
        attempted += 1
        _info(f"Reconnecting USB for {board_platform} ({serial_path}) ...")
        if _usb_reconnect(serial_path):
            _info(f"Board {board_platform}: reconnected OK")
            reconnected += 1
        else:
            _warn(f"Board {board_platform}: reconnect did not confirm device reappearance")

    if attempted == 0:
        _debug("No devices available for pre-flight reconnect")
    else:
        _debug(f"Pre-flight reconnect: {reconnected}/{attempted} devices confirmed")
    print()


def _recover_device(serial_path: str, skip_recovery: bool) -> bool:
    """Attempt sysfs USB reconnect for a missing device. Returns True if recovered."""
    if skip_recovery:
        _debug(f"Recovery skipped for {serial_path} (--skip-recovery)")
        return False

    _info(f"Attempting USB reset for {serial_path} ...")
    if _usb_reconnect(serial_path):
        return True

    _warn(f"USB reset did not recover {serial_path} - device may be physically disconnected")
    return False


def _check_serial_devices(boards: list[dict], skip_recovery: bool) -> list[dict]:
    """Check serial device connectivity for all boards."""
    _sep("Step 2b: Serial device connectivity check")

    if not boards:
        _warn("No connected boards in hardware map")
        return []

    results = []
    for board in boards:
        serial_path = board.get("serial", "")
        board_platform = board.get("platform", "unknown")
        entry = {**board, "serial_ok": False, "status": "UNKNOWN"}

        if not serial_path:
            _warn(f"Board {board_platform}: no serial path configured")
            entry["status"] = "NO_SERIAL"
            results.append(entry)
            continue

        if not Path(serial_path).exists():
            reason = "MISSING"
        elif not _wait_port_stable(serial_path):
            reason = "BUSY"
        else:
            reason = "OK"

        if reason == "OK":
            _info(f"Board {board_platform}: {serial_path} OK")
            entry["serial_ok"] = True
            entry["status"] = "OK"
        else:
            _warn(f"Board {board_platform}: {serial_path} {reason}")
            entry["status"] = reason

            if _recover_device(serial_path, skip_recovery):
                entry["serial_ok"] = True
                entry["status"] = "RECOVERED"
                _info(f"Board {board_platform}: {serial_path} RECOVERED")
            else:
                _warn(f"Board {board_platform}: {serial_path} NOT RECOVERABLE ({reason}) - smoke test will be final verdict")
                entry["status"] = "FAILED"

        results.append(entry)

    ok_count = sum(1 for r in results if r["serial_ok"])
    _info(f"Serial check: {ok_count}/{len(results)} devices OK")
    print()
    return results


# =============================================================================
# Step 3: Board check smoke test
# =============================================================================

def _run_smoke_test(platforms: list[str]) -> bool:
    """Run board_check test via run_regression.py --level board_check."""
    _sep("Step 3: Smoke test (final verification)")

    if not REGRESSION_SCRIPT.exists():
        _error(f"Regression script not found: {REGRESSION_SCRIPT}")
        return False

    cmd = [sys.executable, str(REGRESSION_SCRIPT), "--level", "board_check"]
    for plat in platforms:
        cmd.extend(["--platform", plat])
    _info(f"Running: {' '.join(cmd)}")

    CHECK_TIMEOUT_SEC = 300
    try:
        result = subprocess.run(
            cmd,
            cwd=str(WORKSPACE_ROOT),
            timeout=CHECK_TIMEOUT_SEC,
        )
        if result.returncode == 0:
            _info("Board check test: PASS")
            print()
            return True
        else:
            _warn(f"Board check test: FAIL (exit {result.returncode})")
    except subprocess.TimeoutExpired:
        _error(f"Board check test timed out {CHECK_TIMEOUT_SEC} seconds!")
    except (FileNotFoundError, OSError) as exc:
        _error(f"Failed to run board_check test: {exc}")

    print()
    return False


# =============================================================================
# Step 4: Summary & exit code
# =============================================================================

def _print_summary(
    workspace_ok: bool,
    board_results: list[dict],
    smoke_ok: bool | None,
) -> int:
    """Print final status table and return exit code."""
    _sep("Summary")

    # Workspace
    ws_status = "PASS" if workspace_ok else "FAIL"
    print(f"  Workspace check:  {ws_status}")

    # Board table
    if board_results:
        print()
        print(f"  {'Board':<20} {'Serial':<25} {'Status':<12}")
        print(f"  {'-'*20} {'-'*25} {'-'*12}")
        for b in board_results:
            board_name = b.get("platform", "unknown")
            serial = b.get("serial", "N/A")
            status = b.get("status", "UNKNOWN")
            print(f"  {board_name:<20} {serial:<25} {status:<12}")
        print()
    else:
        print("  No boards found in hardware map.")
        print()

    # Smoke test
    if smoke_ok is None:
        print("  Smoke test:       SKIPPED")
    elif smoke_ok:
        print("  Smoke test:       PASS")
    else:
        print("  Smoke test:       FAIL")

    print()

    if smoke_ok is not None:
        overall_ok = workspace_ok and smoke_ok
    else:
        all_serial_ok = all(b["serial_ok"] for b in board_results) if board_results else False
        overall_ok = workspace_ok and all_serial_ok

    if overall_ok:
        _info("Overall: READY")
        return EXIT_OK
    else:
        _error("Overall: NOT READY")
        return EXIT_FAILURE


# =============================================================================
# CLI
# =============================================================================

def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="HW test bench health check - verify boards are alive before testing",
    )
    parser.add_argument(
        "--skip-smoke",
        action="store_true",
        help="Skip the board_check smoke test (only check serial devices)",
    )
    parser.add_argument(
        "--skip-recovery",
        action="store_true",
        help="Skip reset/power-cycle recovery on failure",
    )
    parser.add_argument(
        "-p", "--platform",
        action="append",
        metavar="BOARD",
        help=(
            f"Supported: {', '.join(sorted(KNOWN_PLATFORMS))}. Omit to run on all"
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    hw_map_path = _resolve_hardware_map()
    # Health check only applies to device-testable platforms (not build-only).
    if args.platform:
        platforms = list(args.platform)
        invalid = [p for p in platforms if _platform_base(p) not in KNOWN_PLATFORMS]
        if invalid:
            _error(f"Unknown platform(s): {', '.join(invalid)}")
            _error(f"Supported: {', '.join(sorted(KNOWN_PLATFORMS))}")
            return EXIT_FAILURE
        build_only_skipped = [p for p in platforms if _platform_base(p) in BUILD_ONLY_PLATFORMS]
        if build_only_skipped:
            _info(f"Skipping build-only platform(s) (no HW): {', '.join(build_only_skipped)}")
            platforms = [p for p in platforms if _platform_base(p) not in BUILD_ONLY_PLATFORMS]
            if not platforms:
                _info("No device-testable platforms remaining. Nothing to check.")
                return EXIT_OK
    else:
        platforms = sorted(DEVICE_PLATFORMS)

    _sep("HW Health Check")
    _info(f"Workspace root:  {WORKSPACE_ROOT}")
    _info(f"Hardware map:    {hw_map_path}")
    _info(f"Platforms:       {', '.join(platforms)}")
    _info(f"Skip smoke test: {args.skip_smoke}")
    _info(f"Skip recovery:   {args.skip_recovery}")
    print()

    # Step 1: Workspace sanity
    workspace_ok = _check_workspace()

    # Step 2: Serial device check
    if not hw_map_path.exists():
        _error(f"Hardware map not found: {hw_map_path}")
        board_results = []
    else:
        boards = _load_hardware_map(hw_map_path)

        # Warn if a requested smoke-test platform is not physically present, but
        # run USB/serial checks on every connected board regardless of platform.
        selected_platforms = {_platform_base(p) for p in platforms}
        map_platforms = {_platform_base(str(b.get("platform", ""))) for b in boards}
        missing = sorted(selected_platforms - map_platforms)
        if missing:
            _warn(f"Selected platform(s) not found in connected hardware map entries: {missing}")

        _preflight_usb_reconnect(boards)
        board_results = _check_serial_devices(boards, args.skip_recovery)

    # Step 3: Smoke test - final verification (always runs; it is the authoritative validator)
    smoke_ok = None
    if not args.skip_smoke:
        smoke_ok = _run_smoke_test(platforms)

    # Step 4: Summary
    return _print_summary(workspace_ok, board_results, smoke_ok)


if __name__ == "__main__":
    sys.exit(main())
