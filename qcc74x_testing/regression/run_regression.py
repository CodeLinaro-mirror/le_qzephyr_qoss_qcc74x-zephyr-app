# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/usr/bin/env python3
"""QCC7xx regression test runner - wrapper around `west twister` for QCC7xx hardware."""

import argparse
import os
import platform
import subprocess
import sys
from pathlib import Path

import yaml

# Make twisterlib importable. qcc74x_testing lives under
# <workspace>/qapp/qcc74x/qcc74x_testing, so locate the west workspace root by
# walking up until we find the sibling `zephyr` directory (works regardless of
# how deeply this tree is nested).
def _find_workspace_root(start: Path) -> Path:
    for parent in [start, *start.parents]:
        if (parent / "zephyr").is_dir() and (parent / ".west").is_dir():
            return parent
    # Fallback: assume the classic layout two levels up.
    return start.parent.parent


_WORKSPACE_ROOT = _find_workspace_root(Path(__file__).resolve().parent)
_ZEPHYR_BASE = _WORKSPACE_ROOT / "zephyr"
_ZEPHYR_SCRIPTS = _ZEPHYR_BASE / "scripts"
_TWISTER_LIB = _ZEPHYR_SCRIPTS / "pylib" / "twister"
if not os.environ.get("ZEPHYR_BASE"):
    os.environ["ZEPHYR_BASE"] = str(_ZEPHYR_BASE)

for _p in (_ZEPHYR_SCRIPTS, _TWISTER_LIB):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))

from twisterlib.hardwaremap import HardwareMap  # noqa: E402

# =============================================================================
# Constants
# =============================================================================

SCRIPT_DIR = Path(__file__).resolve().parent
WORKSPACE_ROOT = _WORKSPACE_ROOT
QUARANTINE_PATH = SCRIPT_DIR / "quarantine.yaml"
TEST_CONFIG_PATH = SCRIPT_DIR / "test_config_extended.yaml"
DEFAULT_HARDWARE_MAP = Path.home() / ".zephyr-twister" / "hardware-map.yaml"
CI_HARDWARE_MAP = SCRIPT_DIR.parent / "hw_management" / "ci-hardware-map.yaml"
TWISTER_OUTDIR = WORKSPACE_ROOT / "twister-out"
DEFAULT_LEVEL = "smoke"

# Boards that require a physical device for testing (flash + run).
DEVICE_PLATFORMS = frozenset({"qcc743m_evk", "qcc744m_evk", "qcc748m_evk"})
# Boards that are build-only (no hardware available for device testing).
BUILD_ONLY_PLATFORMS = frozenset({"qcc75xm_evk"})
# All boards supported by this script.
KNOWN_PLATFORMS = DEVICE_PLATFORMS | BUILD_ONLY_PLATFORMS

EXIT_OK = 0
EXIT_ARG_ERROR = 1
EXIT_HW_ERROR = 2
EXIT_TEST_FAILURE = 3


# =============================================================================
# Hardware map resolution
# =============================================================================

def _resolve_hardware_map() -> Path:
    """Return the hardware map path to use.

    When running on the CI machine, use hardware map from the repository.
    Otherwise fall back to the developer-local default location.
    """
    if os.environ.get("QCC_CI_MACHINE"):
        print(f"CI environment detected - using CI hardware map: {CI_HARDWARE_MAP}")
        return CI_HARDWARE_MAP
    print(f"Using default hardware map: {DEFAULT_HARDWARE_MAP}")
    return DEFAULT_HARDWARE_MAP


# =============================================================================
# Config helpers
# =============================================================================

def _load_test_config(config_path: Path) -> dict:
    """Load test_config_extended.yaml. Exits on error."""
    if not config_path.exists():
        print(f"ERROR: Test config not found: {config_path}")
        sys.exit(EXIT_ARG_ERROR)
    try:
        with open(config_path, encoding="utf-8") as f:
            data = yaml.safe_load(f)
    except yaml.YAMLError as exc:
        print(f"ERROR: Failed to parse {config_path}: {exc}")
        sys.exit(EXIT_ARG_ERROR)
    if not isinstance(data, dict) or "levels" not in data:
        print(f"ERROR: {config_path} must contain a 'levels' key")
        sys.exit(EXIT_ARG_ERROR)
    levels = data["levels"]
    if not isinstance(levels, list):
        print(f"ERROR: 'levels' in {config_path} must be a list of entries")
        sys.exit(EXIT_ARG_ERROR)
    return {lvl["name"]: lvl for lvl in levels if isinstance(lvl, dict) and "name" in lvl}


def _resolve_dirs(level_config: dict, workspace_root: Path) -> list[Path]:
    """Convert relative dir strings to absolute Paths and validate they exist."""
    dirs = []
    for entry in level_config.get("dirs", []):
        p = workspace_root / entry
        if not p.is_dir():
            print(f"ERROR: Directory not found: {p}")
            sys.exit(EXIT_ARG_ERROR)
        dirs.append(p)
    return dirs


# =============================================================================
# HardwareMapReader
# =============================================================================

def _make_hwmap_env() -> argparse.Namespace:
    """Build the minimal Namespace that HardwareMap.load() needs."""
    options = argparse.Namespace(
        device_flash_timeout=60,
        device_flash_with_test=False,
        flash_before=True,
    )
    return argparse.Namespace(options=options)


class HardwareMapReader:
    """Thin wrapper around Zephyr's twisterlib.HardwareMap for reading hardware-map.yaml."""

    def __init__(self, map_path: Path):
        self.map_path = map_path
        self._hwmap = HardwareMap(_make_hwmap_env())

    def load(self) -> bool:
        """Load the hardware map. Returns False if file is missing or malformed."""
        try:
            self._hwmap.load(str(self.map_path))
            return True
        except Exception:
            return False

    def get_connected_platforms(self) -> list[str]:
        """Return platform names for all connected DUTs."""
        return [dut.platform for dut in self._hwmap.duts if dut.connected]

    def print_summary(self) -> None:
        """Print a human-readable summary of connected hardware."""
        if not self._hwmap.duts:
            print("  (hardware map is empty or has no connected devices)")
            return
        self._hwmap.dump(connected_only=False)


# =============================================================================
# CommandBuilder
# =============================================================================

class CommandBuilder:
    """Constructs west twister command lines."""

    def __init__(
        self,
        quarantine_path: Path,
        hardware_map_path: Path,
        extra_args: list[str] | None = None,
    ):
        self.quarantine_path = quarantine_path
        self.hardware_map_path = hardware_map_path
        self.extra_args = extra_args or []

    def _base(self, platforms: list[str], outdir: Path) -> list[str]:
        """Start a command: west twister + platform flags + output dir."""
        cmd = ["west", "twister"]
        if platform.system() == "Windows":
            cmd.append("--short-build-path")
        for plat in platforms:
            cmd.extend(["-p", plat])
        cmd.extend(["-O", str(outdir)])
        return cmd

    def _add_mode(self, cmd: list[str], build_only: bool) -> None:
        """Append device-testing flags or --build-only."""
        if build_only:
            cmd.append("--build-only")
        else:
            cmd.extend(["--device-testing", "--flash-before"])
            cmd.extend(["--hardware-map", str(self.hardware_map_path)])

    def _finalize(self, cmd: list[str]) -> list[str]:
        """Add common reporting/retry args and forwarded extra args."""
        cmd.extend(["--quarantine-list", str(self.quarantine_path)])
        cmd.extend(["--report-filtered"])
        cmd.extend(["--detailed-skipped-report"])
        cmd.append("-vvv")
        cmd.extend(["--inline-logs", "--log-level", "DEBUG"])
        cmd.extend(["--retry-build-errors"])
        cmd.extend(["--retry-failed", "2"])
        cmd.extend(["--retry-interval", "2"])
        cmd.extend(["--timestamps"])
        cmd.extend(["--platform-reports"])
        cmd.extend(["--report-all-options"])
        cmd.extend(["--clobber-output"])
        cmd.extend(self.extra_args)
        return cmd

    def build_command(
        self,
        platforms: list[str],
        dirs: list[Path],
        build_only: bool,
        outdir: Path,
    ) -> list[str]:
        """Build a complete twister command."""
        cmd = self._base(platforms, outdir)
        for d in dirs:
            cmd.extend(["-T", str(d)])
        self._add_mode(cmd, build_only)
        return self._finalize(cmd)


# =============================================================================
# Helpers
# =============================================================================

def _platform_base(plat: str) -> str:
    """Strip the variant qualifier from a platform name."""
    return plat.split("/")[0]


def _sep(title: str = "") -> None:
    line = "=" * 80
    print(line)
    if title:
        print(title)
        print(line)


def _print_yaml_file(title: str, file_path: Path) -> None:
    """Print a YAML file contents under a section header, stripping comment lines."""
    _sep(title)
    print(f"File: {file_path}")
    if not file_path.exists():
        print("  Not found.")
        print()
        return
    try:
        with open(file_path, encoding="utf-8") as f:
            for line in f:
                stripped = line.rstrip()
                if not stripped.lstrip().startswith("#"):
                    print(stripped)
    except OSError as exc:
        print(f"  ERROR reading file: {exc}")
    print()


def _print_west_command(cmd: list[str]) -> None:
    """Print west twister command with forward slashes for cross-platform display."""
    formatted = [arg.replace("\\", "/") for arg in cmd]
    print(" ".join(formatted))
    print()


def _run_command(cmd: list[str]) -> int:
    """Print the command and execute it. Returns exit code."""
    _sep("TWISTER COMMAND")
    _print_west_command(cmd)

    _sep()
    try:
        env = os.environ.copy()
        env.setdefault("PYTHONUTF8", "1")
        env.setdefault("PYTHONIOENCODING", "utf-8")

        with subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            cwd=str(WORKSPACE_ROOT),
            env=env,
        ) as proc:
            if proc.stdout:
                for line in iter(proc.stdout.readline, ""):
                    print(line, end="")
            proc.wait()
            return proc.returncode
    except FileNotFoundError:
        print("ERROR: 'west' not found. Activate the .venv first.")
        return EXIT_ARG_ERROR
    except subprocess.SubprocessError as exc:
        print(f"ERROR: subprocess failed: {exc}")
        return EXIT_ARG_ERROR


def _print_result(rc: int) -> int:
    """Print success/failure banner. Returns appropriate exit code."""
    print()
    if rc == 0:
        _sep("COMPLETED SUCCESSFULLY")
    else:
        _sep(f"FAILED (twister exit code: {rc})")
        print("Check twister-out/ for detailed logs.")
    return EXIT_OK if rc == 0 else EXIT_TEST_FAILURE


# =============================================================================
# Mode handlers
# =============================================================================

def _handle_level(
    level_name: str,
    platforms: list[str],
    builder: CommandBuilder,
    build_only: bool,
    hw_available: bool,
    hardware_map_path: Path,
) -> int:
    """Run a level defined in test_config_extended.yaml.

    In device-testing mode two twister invocations are issued when the platform
    list contains both device-capable boards and build-only boards:

      * Device platforms  → ``twister-out/{level}-device/``  (--device-testing)
      * Build-only boards → ``twister-out/{level}-build/``   (--build-only)

    In --build-only mode a single invocation covers all platforms and writes to
    ``twister-out/{level}-build/``.
    """
    config = _load_test_config(TEST_CONFIG_PATH)

    if level_name not in config:
        avail = ", ".join(sorted(config.keys()))
        print(f"ERROR: Unknown level '{level_name}'.")
        print(f"  Available levels: {avail}")
        return EXIT_ARG_ERROR

    level_config = config[level_name]
    dirs = _resolve_dirs(level_config, WORKSPACE_ROOT)

    # Split requested platforms by capability.
    device_plats = [p for p in platforms if _platform_base(p) in DEVICE_PLATFORMS]
    hw_build_plats = [p for p in platforms if _platform_base(p) in BUILD_ONLY_PLATFORMS]

    # Hardware map is only required when there are device platforms to run.
    if not build_only and device_plats and not hw_available:
        print(f"ERROR: Hardware map not found: {hardware_map_path}")
        print("  Connect hardware or use --build-only to run without a board.")
        return EXIT_HW_ERROR

    # Each entry: (mode_label, platforms, outdir, twister_rc)
    runs: list[tuple[str, list[str], Path, int]] = []

    def _run_one(plats: list[str], is_build_only: bool, suffix: str, mode_label: str) -> None:
        _sep(f"LEVEL MODE  |  level: {level_name}  |  {mode_label}  |  boards: {', '.join(plats)}")
        print(f"Description: {level_config.get('description', '').strip()}")
        print(f"  Directories: {len(dirs)}")
        for d in dirs:
            print(f"    {d.relative_to(WORKSPACE_ROOT)}")
        print()
        outdir = TWISTER_OUTDIR / f"{level_name}-{suffix}"
        cmd = builder.build_command(plats, dirs=dirs, build_only=is_build_only, outdir=outdir)
        rc = _run_command(cmd)
        _print_result(rc)
        runs.append((mode_label, plats, outdir, rc))

    if build_only:
        # All platforms into one build-only run.
        _run_one(platforms, is_build_only=True, suffix="build", mode_label="build-only")
    else:
        # Device testing for hardware-capable boards.
        if device_plats:
            _run_one(device_plats, is_build_only=False, suffix="device", mode_label="device testing (on real HW)")

        # Build-only boards always get their own separate run and output directory,
        # even when the overall mode is device testing.
        if hw_build_plats:
            print(
                f"WARNING: {', '.join(hw_build_plats)} has no hardware - "
                f"running as separate build-only job (twister-out/{level_name}-build/)."
            )
            _run_one(hw_build_plats, is_build_only=True, suffix="build", mode_label="build-only (no HW)")

    if not runs:
        print("WARNING: No platforms to run.")
        return EXIT_OK

    exit_codes = [rc for _, _, _, rc in runs]
    worst_rc = max(exit_codes)

    # -------------------------------------------------------------------------
    # Execution summary
    # -------------------------------------------------------------------------
    _sep("EXECUTION SUMMARY")
    print(f"  Level       : {level_name}")
    print(f"  Description : {level_config.get('description', '').strip()}")
    print(f"  Directories : {len(dirs)}")
    for d in dirs:
        print(f"               {d.relative_to(WORKSPACE_ROOT)}")
    print()
    for idx, (mode_label, plats, outdir, rc) in enumerate(runs, start=1):
        status = "PASSED" if rc == 0 else f"FAILED (twister rc={rc})"
        print(f"  Run {idx}: {mode_label}")
        print(f"    Boards : {', '.join(plats)}")
        print(f"    Output : {outdir.relative_to(WORKSPACE_ROOT)}")
        print(f"    Result : {status}")
        print()
    if len(runs) > 1:
        overall = "PASSED" if worst_rc == 0 else f"FAILED ({sum(1 for rc in exit_codes if rc != 0)} of {len(runs)} runs failed)"
        print(f"  Overall     : {overall}")
        print()
    _sep()

    return EXIT_OK if worst_rc == 0 else EXIT_TEST_FAILURE


def _handle_list(hw_reader: HardwareMapReader, hw_map_loaded: bool) -> int:
    """Show hardware map, test config, and quarantine info."""
    _sep("HARDWARE MAP")
    if hw_map_loaded:
        print(f"File: {hw_reader.map_path}")
        hw_reader.print_summary()
    else:
        print(f"Not found: {hw_reader.map_path}")
    print()

    _print_yaml_file("TEST CONFIG (levels)", TEST_CONFIG_PATH)
    _print_yaml_file("QUARANTINE", QUARANTINE_PATH)
    return EXIT_OK


# =============================================================================
# Main
# =============================================================================

def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="QCC7xx regression test runner",
    )

    mode = parser.add_mutually_exclusive_group(required=False)
    mode.add_argument(
        "--level",
        metavar="LEVEL",
        default=None,
        help=(
            f"Run a level from test_config_extended.yaml (default: '{DEFAULT_LEVEL}'). "
            f"Use --list to see available levels."
        ),
    )
    mode.add_argument(
        "--list",
        action="store_true",
        help="Show hardware map, levels, and active quarantine",
    )

    parser.add_argument(
        "--build-only",
        action="store_true",
        help="Build without flashing. Device testing is the default.",
    )

    parser.add_argument(
        "-p", "--platform",
        action="append",
        metavar="BOARD",
        help=(
            f"Supported: {', '.join(sorted(KNOWN_PLATFORMS))}. Omit to run on all"
        ),
    )

    # Everything after '--' is forwarded to twister unchanged
    args, extra = parser.parse_known_args()
    args.extra = [a for a in extra if a != "--"]
    unknown_flags = [a for a in args.extra if a.startswith("-")]
    if unknown_flags:
        print(
            f"WARNING: Unrecognized option(s) will be forwarded to Twister: "
            f"{' '.join(unknown_flags)}\n"
            f"  If unintentional, check your spelling."
        )
    return args


def main() -> int:
    args = _parse_args()

    if not QUARANTINE_PATH.exists():
        print(f"ERROR: Quarantine file not found: {QUARANTINE_PATH}")
        return EXIT_ARG_ERROR
    print(f"Quarantine:  {QUARANTINE_PATH}")

    hardware_map = _resolve_hardware_map()
    print(f"Hardware map: {hardware_map}")

    hw_reader = HardwareMapReader(hardware_map)
    hw_map_loaded = hw_reader.load()

    # List mode
    if args.list:
        return _handle_list(hw_reader, hw_map_loaded)

    # Resolve and validate platforms
    if args.platform:
        platforms = list(args.platform)
        invalid = [p for p in platforms if _platform_base(p) not in KNOWN_PLATFORMS]
        if invalid:
            print(f"ERROR: Unknown platform(s): {', '.join(invalid)}")
            print(f"  Supported: {', '.join(sorted(KNOWN_PLATFORMS))}")
            return EXIT_ARG_ERROR
        # If user selected build-only platform(s) for a device run, warn - _handle_level
        # will route them to a separate build-only invocation automatically.
        if not args.build_only:
            build_only_selected = [p for p in platforms if _platform_base(p) in BUILD_ONLY_PLATFORMS]
            if build_only_selected:
                print(
                    f"WARNING: {', '.join(build_only_selected)} has no hardware - "
                    "will run as a separate build-only job."
                )
    else:
        # Default: always include all known platforms.
        # _handle_level splits them into device-testing and build-only runs as needed.
        platforms = sorted(KNOWN_PLATFORMS)

    # Warn if any expected device board is missing from the HW map during device runs
    if hw_map_loaded and not args.platform and not args.build_only:
        connected = {_platform_base(p) for p in hw_reader.get_connected_platforms()}
        missing = sorted(DEVICE_PLATFORMS - connected)
        if missing:
            print(f"WARNING: Expected board(s) not found in HW map: {missing}")

    print(f"Platforms:   {', '.join(platforms)}")

    builder = CommandBuilder(
        quarantine_path=QUARANTINE_PATH,
        hardware_map_path=hardware_map,
        extra_args=args.extra,
    )

    level = args.level if args.level is not None else DEFAULT_LEVEL
    if args.level is None:
        print(f"No arguments - default: --level {DEFAULT_LEVEL} (device testing on all boards)")
        print()
    return _handle_level(level, platforms, builder, args.build_only, hw_map_loaded, hardware_map)


if __name__ == "__main__":
    sys.exit(main())
