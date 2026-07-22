# QCC7xx Testing Guide

This document is a single entry point for running tests in this workspace and for finding the files that control board setup, hardware wiring, Twister execution, regression levels, and reports.

The workspace is a West workspace with Zephyr `v4.1.0`. Run commands from the workspace root and activate `.venv` first.

## 1. Workspace map

Use these repositories for different parts of the flow:

| Repository              | Purpose                                                                                                                              |
|-------------------------|--------------------------------------------------------------------------------------------------------------------------------------|
| `qcc74x_zephyr_unikie/` | QCC74x board support, SoC support, custom drivers, flash runner, Bitbucket pipeline definitions                                      |
| `qcc74x_testing/`       | QCC-specific samples and tests that Twister discovers and runs, regression wrapper, hardware bench utilities, hardware maps, reports |
| `zephyr/`               | Modified Zephyr tree used by this workspace                                                                                          |
| `modules/hal/hal_qcom/` | Qualcomm HAL used by the Zephyr wrappers                                                                                             |

The most important paths for test execution are:

| Path                                                       | Why it matters                                                               |
|------------------------------------------------------------|------------------------------------------------------------------------------|
| `qcc74x_testing/regression/run_regression.py`              | Main wrapper for multi-test regression runs                                  |
| `qcc74x_testing/regression/test_config_extended.yaml`      | Defines regression test levels such as `smoke`, `commit`, `merge`, `nightly` |
| `qcc74x_testing/regression/quarantine.yaml`                | Tests skipped globally during regression                                     |
| `qcc74x_testing/hw_management/board-connections.md`        | Physical bench wiring and jumper documentation                               |
| `qcc74x_testing/hw_management/ci-hardware-map.yaml`        | CI Twister hardware map                                                      |
| `~/.zephyr-twister/hardware-map.yaml`                      | Local Twister hardware map used on a developer machine                       |
| `qcc74x_testing/samples/`                                  | QCC-specific samples                                                         |
| `qcc74x_testing/tests/`                                    | QCC-specific tests                                                           |

## 2. Environment assumptions

Before running anything:

- Start in the workspace root.
- Activate `.venv`.
- Prefer `west build`, `west flash`, `west twister`, or `python qcc74x_testing/regression/run_regression.py`.
- Do not treat this as a standalone Zephyr application. It is a West workspace.

Linux shell example:

```bash
cd /home/path_to_your_repo
source .venv/bin/activate
```

Remember to replace <username> with your username in the paths.

> **`qcc74x_es` (Zephyr 4.4) workspace only:** also run
> `source qapp/qcc74x/qcc74x_testing/regression/env.sh` once per shell,
> right after activating `.venv`. Without it, `west twister` fails to
> discover any testsuites and every build errors out with an
> `--edtlib-Werror` CMake error — both are Zephyr `v4.4.0` fork issues
> unrelated to `qcc74x_testing` itself. Not needed on the legacy
> `workspace/` tree (Zephyr `v4.1.0`). See
> `claude_doc/qcc74x_testing_twister_fix.md` for details.
> `west build` is unaffected either way.

## 3. Supported boards for testing

Boards used by this workspace:

| Board         | Typical use                                |
|---------------|--------------------------------------------|
| `qcc743m_evk` | Device testing on real hardware            |
| `qcc744m_evk` | Device testing on real hardware            |
| `qcc748m_evk` | Device testing on real hardware            |
| `qcc75xm_evk` | Build-only platform, no hardware execution |


## 4. Hardware setup for Twister device runs

Please start with boards setup and connections. Please connect your boards same way as it is done for CI and described in: `qcc74x_testing/hw_management/board-connections.md` which was created for following setup `qcc74x_testing/hw_management/ci-hardware-map.yaml` . Next, for device testing, Twister needs a hardware map mentioned before (ci-hardware-map.yaml). You need one describing your local setup you have. On Linux it shall be stored in:

```text
~/.zephyr-twister/hardware-map.yaml
```

See more details in `qcc74x_testing/regression/README.md`. Example of such file is stored in `qcc74x_testing/hw_management/ci-hardware-map.yaml` and it used for CI machine. It is good to create udev rules for serial ports you use to keep serial ports persistent between PC restarts.

## 5. Running one test or one sample with Twister

Use plain `west twister` when you want to run a single testsuite root instead of a whole regression level.

Device-testing example:

```bash
source .venv/bin/activate

west twister -p qcc743m_evk -p qcc744m_evk -p qcc748m_evk -T zephyr/samples/hello_world --device-testing --flash-before --hardware-map /home/<username>/.zephyr-twister/hardware-map.yaml --quarantine-list qcc74x_testing/regression/quarantine.yaml --report-filtered --detailed-skipped-report -vvv --inline-logs --log-level DEBUG --retry-build-errors --retry-failed 2 --retry-interval 2 --timestamps --platform-reports --report-all-options --clobber-output
```

> Note: Please note that hardware map is used here stored in `/home/<username>/.zephyr-twister/hardware-map.yaml`. Please create one and point to it on your local machine.

Build-only example:

```bash
source .venv/bin/activate
west twister -p qcc743m_evk -p qcc744m_evk -p qcc748m_evk -p qcc75xm_evk -T zephyr/samples/hello_world --build-only --quarantine-list qcc74x_testing/regression/quarantine.yaml --report-filtered --detailed-skipped-report -vvv --inline-logs --log-level DEBUG --retry-build-errors --retry-failed 2 --retry-interval 2 --timestamps --platform-reports --report-all-options --clobber-output
```

> Note: Please note that hardware map is used here stored in `/home/<username>/.zephyr-twister/hardware-map.yaml`. Please create one and point to it on your local machine.

Use this flow when:

- You are developing one specific case.
- You need faster iteration than a full regression level.
- You want to confirm that the problem is local to one testsuite root.

## 6. Running the regression wrapper

`qcc74x_testing/regression/run_regression.py` is the normal entry point for broad local and CI runs. See more details in `qcc74x_testing/regression/README.md`.

What it does:

- Selects `-T` test roots from `test_config_extended.yaml`
- Applies `quarantine.yaml` automatically
- Uses device testing for hardware-capable boards
- Splits build-only boards into a separate build-only Twister invocation
- Writes results under `twister-out/<level>-device/` and `twister-out/<level>-build/`
