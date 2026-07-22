# QCC7xx Regression Runner (`run_regression.py`)

This document describes how to run `qcc7xx_unikie_utils/regression/run_regression.py` for QCC743M/QCC744M regression testing.

> **Board-specific peripheral test overlays:** `../board_overlays/` holds `qcc748m_evk` board
> configuration (`.conf`/`.overlay`/`.dtsi`) for a few upstream Zephyr driver tests
> (`gpio_api_1pin`, `adc_api`, `spi_loopback`) that don't ship qcc74x board support upstream.
> Since `zephyr/` must stay unmodified, inject these files via `west twister`'s `-x` flag
> instead of placing them under the test's own `boards/` directory. Example (from the workspace
> root, with the venv activated — **paths passed to `-x` must be absolute**, twister's internal
> CMake invocation runs from a different working directory and will not resolve relative ones;
> forward slashes work fine on Windows too and avoid shell-escaping surprises):
>
> ```powershell
> west twister -p qcc748m_evk/qcc748 -T zephyr/tests/drivers/adc/adc_api -s drivers.adc `
>   -x DTC_OVERLAY_FILE=C:/workspace/qcc74x/sdk/qcc74x_es_sdk/qapp/qcc74x/qcc74x_testing/board_overlays/adc_api/qcc748m_evk.overlay `
>   -x EXTRA_CONF_FILE=C:/workspace/qcc74x/sdk/qcc74x_es_sdk/qapp/qcc74x/qcc74x_testing/board_overlays/adc_api/qcc748m_evk.conf `
>   --build-only
> ```
>
> Make sure `ZEPHYR_SDK_INSTALL_DIR` in your shell points at this workspace's SDK
> (`C:\workspace\qcc74x\tools\zephyr-sdk-1.0.1`) before running — a stale value inherited from
> another Zephyr workspace will make CMake reject the SDK version.

## What this script does

`run_regression.py` is a wrapper around `west twister` that:

- selects test directories based on levels defined in `qcc7xx_unikie_utils/regression/test_config_extended.yaml`
- always applies quarantine entries from `qcc7xx_unikie_utils/regression/quarantine.yaml`
- supports board selection for:
    - `qcc743m_evk`
    - `qcc744m_evk`
    - `qcc748m_evk`
    - `qcc75xm_evk` (build-only, no hardware testing)
- supports both device testing (via Twister hardware map) and build-only mode
- **all tests started by script shall pass**. If you see test fails it shall be disabled via `qcc7xx_unikie_utils/regression/quarantine.yaml` and enabled again once fixed.

## Prerequisites

- Activate the workspace virtual environment (`.venv`).
- Run commands from the workspace root.
- **On the `qcc74x_es` (Zephyr 4.4) workspace only:** also run
  `source qapp/qcc74x/qcc74x_testing/regression/env.sh` once per shell,
  after activating the venv. This works around two Zephyr `v4.4.0`
  (Qualcomm Gerrit mirror) fork issues that otherwise make `west twister`
  fail with no testsuites discovered, or fail every build with an
  `--edtlib-Werror` CMake error — see
  `claude_doc/qcc74x_testing_twister_fix.md` for the root-cause analysis.
  Not needed on the legacy `workspace/` tree (Zephyr `v4.1.0`), which
  doesn't have this fork issue.
- For device testing, create a Twister hardware map (details below).

## Hardware map requirements

For device runs (without `--build-only`), the script requires a valid Twister hardware map.
By default, this file is not created automatically. You must create it before running device tests.
In most setups, you create it once and update it only if COM/serial assignments change.

Default location:

- Windows: `%USERPROFILE%\\.zephyr-twister\\hardware-map.yaml`
- Linux: `$HOME/.zephyr-twister/hardware-map.yaml`

Expected content pattern:

```yaml
- connected: true
  id: "N/A"
  platform: qcc743m_evk
  product: "Qualcomm QCC743m board"
  runner: qconn_flash
  runner_params:
    - --port
    - COM6
  serial: COM6
- connected: true
  id: "N/A"
  platform: qcc744m_evk
  product: "Qualcomm QCC744m board"
  runner: qconn_flash
  runner_params:
    - --port
    - COM9
  serial: COM9
- connected: true
  id: "N/A"
  platform: qcc748m_evk
  product: "Qualcomm QCC748m board"
  runner: qconn_flash
  runner_params:
    - --port
    - COM10
  serial: COM10
```

Notes:

- In most cases, only the serial/COM port values need to be adjusted.
- `platform` should use the base board name (`qcc743m_evk`, `qcc744m_evk`, `qcc748m_evk`).
- `qcc75xm_evk` does not need a hardware map entry (build-only platform).

## Required board hardware configuration (J3/J4)

Use the known-good jumper setup below for regression runs. Keep this setup stable across runs.

Important:

- do not connect additional external wiring unless a specific test requires it
- for normal regression runs, no extra connections should be attached to test GPIOs
- incorrect jumper setup can cause signal contention and false software failures

J3 connected pins:

- `(1,2)`
- `(11,12)`
- `(13,14)`

J4 connected pins:

- `(1,2)`
- `(3,5)`
- `(4,6)`
- `(9,11)`
- `(12,14)`
- `(17,18)`

Additional connection for this regression flow:

- connect J3 pin `18` to J4 pins `(9,11)` so all three points are electrically tied
- this is required for this regression script flow (automatic flash/run)
- for normal daily development and log viewing, keep J3 pin `(18)` disconnected

Practical wiring suggestions:

- if you use standard male-female jumper wires, mechanical fit may be loose because these headers appear to use `2.00 mm` pitch (not standard `2.54 mm`)
- for a temporary connection, you can remove the plastic housing from a female jumper cable and gently crimp/tighten the metal contact with pliers. Connector tighten this way will fit better J3 / J4 pins.
- another practical method is to bridge two jumper caps with a short wire and use that as the interconnect between required header points.
- treat these as practical lab suggestions; choose the method that gives stable electrical contact and repeatable test runs.

## Configuration files

The script uses two YAML files:

| File                        | Role                                                       |
|-----------------------------|------------------------------------------------------------|
| `test_config_extended.yaml` | Defines levels and which **directories** to scan per level |
| `quarantine.yaml`           | Lists scenarios excluded from all runs                     |

`quarantine.yaml` is a native Zephyr/Twister feature. `test_config_extended.yaml` is a project-specific file consumed only by this script.

### test_config_extended.yaml - level directory mapping

Each level entry is a list item with:

- `name` - unique identifier used with `--level`
- `description` - human-readable summary
- `dirs` - workspace-relative directories passed as `-T` to Twister; all test scenarios found in each directory are discovered and run


### quarantine.yaml - test exclusion list

Passed directly to Twister via `--quarantine-list`. Any scenario listed here is skipped regardless of level. Use this to temporarily exclude known-failing tests.

## Available levels

Check `test_config_extended.yaml`. The default level (no arguments) is `smoke`.

### Smoke tests

Smoke tests are a small, fast subset of tests used as an initial health check.

Purpose of the smoke level in this project:

- quickly validate basic integration on both boards
- catch obvious regressions early, before wider execution
- keep local pre-PR checks short

Smoke test directories are defined in `test_config_extended.yaml` under level `smoke`. Each entry is a single leaf test directory; Twister runs all scenarios found there.

## Script usage

### Twister pass-through arguments

`run_regression.py` accepts a fixed set of script options. Any additional unknown options are passed directly to `west twister`.

Why this matters:

- you can keep one stable entry command for regression runs
- you can still use Twister-native features without modifying this script

Usage pattern:

```powershell
python qcc7xx_unikie_utils\regression\run_regression.py [script options] [twister options]
```

Note:

- if a passed Twister option conflicts with script-generated options, command behavior depends on Twister argument precedence
- use `--list` first when diagnosing configuration issues


### Common commands

Activate environment:

```powershell
.\.venv\Scripts\Activate.ps1
```

Show hardware map, active levels, and quarantine. Can be used to verify setup:

```powershell
python qcc7xx_unikie_utils\regression\run_regression.py --list
```

Run default smoke level on both boards:

```powershell
python qcc7xx_unikie_utils\regression\run_regression.py
```

Run smoke level only on QCC743:

```powershell
python qcc7xx_unikie_utils\regression\run_regression.py -p qcc743m_evk
```

Run commit level on both boards:

```powershell
python qcc7xx_unikie_utils\regression\run_regression.py --level commit
```
