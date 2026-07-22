# Enhanced Twister HTML Report Generator

## Purpose

`generate_report.py` converts Zephyr Twister output into a single self-contained HTML report.
It replaces the default `junit2html` output with a feature-coverage dashboard, per-suite log
viewers, image galleries, and manual test support.

---

## Requirements

Python 3.8+ with `pyyaml` (available in CI `.venv`).

---

## Usage

```bash
# Default - auto-discovers all .json files under reports/tests/
python generate_report.py

# Add a fresh Twister output directory (includes handler.log / build.log)
python generate_report.py twister-out/merge-device/

# Multiple directories merged into one report
python generate_report.py twister-out/merge-device/ /mnt/camera-run/
```

Output is always written to `reports/coverage/qcc7xx_test_report_YYYY_MM_DD_HH_MM_SS.html`.

### Paths (all hardcoded relative to workspace root)

| Setting | Path |
|---|---|
| Auto-discovery root | `qcc7xx_samples/reports/tests/` |
| Output directory | `qcc7xx_samples/reports/coverage/` |
| Manifest directory | `qcc74xzephyrporting/` |
| Feature map | `qcc7xx_unikie_utils/reporting/feature_map.yaml` |

---

## Input Sources

### 1. Auto-discovered standalone result files (`qcc7xx_samples/reports/tests/`)

Drop Twister result JSON files anywhere under `qcc7xx_samples/reports/tests/` (subdirectories allowed).
Any file ending in `.json` that contains a `testsuites` key is loaded automatically.
These standalone files have no associated logs - handler.log, build.log, and images
will not appear in the report unless placed alongside the JSON (see manual in `qcc7xx_samples/reports/tests/README.md`).

```text
reports/tests/
└── drivers/
    └── uart/
        └── uart_uart_dma_qcc743m_evk_20251121.json
```

### 2. Twister output directories (passed as arguments)

Standard Twister output layout produced by `west twister`. Log files and images are read
from the directory tree and embedded in the report.

```text
twister-out/merge-device/
├── twister.json
├── qcc743m_evk_qcc743/
│   └── zephyr/
│       └── {test_name}/
│           ├── handler.log        # Device serial output
│           ├── build.log          # CMake/ninja build output (failed only)
│           └── *.png / *.jpg      # Images shown in gallery
├── qcc744m_evk_qcc744/
│   └── zephyr/ ...
└── qcc748m_evk_qcc748/
    └── zephyr/ ...
```

---

## Logs and images

For each test suite the report can show:

- **handler.log** - raw serial output from the device, shown as a collapsible block.
- **build.log** - CMake/ninja build output, shown only for failed or error suites.
- **Images** - any `.png`, `.jpg`, `.jpeg`, `.gif`, `.bmp`, `.webp`, or `.svg` file found
  in the test output directory

For manually added tests the log and images are read from the same directory as the JSON file.

---

## Filtering Logic

| Testsuite `status` | Shown in report? |
|--------------------|------------------|
| `passed`           | Yes              |
| `failed`           | Yes              |
| `error`            | Yes              |
| `filtered`         | No               |
| Anything else      | No               |

Within each shown testsuite, individual test cases:

| Testcase `status` | Shown? |
|-------------------|--------|
| `passed`          | Yes    |
| `failed`          | Yes    |
| `skipped`         | No     |
| `blocked`         | No     |

---

## Feature Coverage Dashboard

The report includes a **Feature Coverage** view (toggle button in the top-right corner)
that shows, for each hardware feature listed in the board yaml `supported:` field, whether
there is a passing test in the current results.

| Status          | Meaning                                             |
|-----------------|-----------------------------------------------------|
| COVERED (green) | At least one matching test suite ran and passed     |
| FAILING (amber) | At least one matching test suite ran but all failed |
| GAP (red)       | No test suite matching this feature ran at all      |

Coverage is computed as a superset across all boards: a feature counts as COVERED if any
board has a passing test for it.

Clicking a feature row pre-fills the search box in the Test Results view with the
corresponding patterns, so you can immediately see which suites cover it.

---

## feature_map.yaml

`feature_map.yaml` tells the report generator how to find test suites that cover each
hardware feature. Without it, matching uses only the bare feature name as a word-boundary
regex, which works for simple names like `adc` or `pwm` but fails when tests live under a
different path (e.g. the feature is `serial` but tests are under `tests/drivers/uart/`).

### Custom features

Features not listed in any board's `supported:` field (e.g. custom drivers with manual
tests only) can be added under a top-level `custom_features:` key in the YAML. They
appear in a separate section of the Coverage table and bypass board validation.

```yaml
custom_features:
  sdio:
    - sdio
```

---

## Duplicate handling

If the same `twister.json` is loaded more than once, the script detects duplicates by
comparing resolved file paths and skips the second copy with a warning:

```text
[skip] duplicate source (already loaded): twister-out/nightly-device/twister.json
```

---
