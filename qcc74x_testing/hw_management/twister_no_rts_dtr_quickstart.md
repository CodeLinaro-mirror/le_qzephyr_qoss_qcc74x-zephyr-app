# Twister No-RTS/DTR Mode – Quick Start

How to run `west twister` device testing on QCC748 without RTS/DTR wiring.

## Prerequisites

1. **Board must have a shell-capable firmware already flashed** (e.g. any app with `CONFIG_SHELL=y`). The pre-flash script sends `platform reboot --edl\r\n` via UART to enter download mode – this only works if the running firmware can process that command.

2. Create a hardware map YAML file locally (not committed to repo) – see example below.

## Hardware Map (`ci-hardware-map-win.yaml`)

```yaml
- connected: true
  id: "qcc748m_evk_1_2"
  platform: qcc748m_evk
  product: "Qualcomm QCC748m board"
  runner: qconn_flash
  runner_params:
    - --port
    - COM7
    - --reset
  serial: COM7
  flash_before: true
  pre_script: ".venv\\Scripts\\python.exe qapp/qcc74x/qcc74x_testing/regression/scripts/qconn_pre_flash.py COM7"
```

Key fields:
- `pre_script` – sends EDL command before flash
- `flash_before: true` – flash first, then open serial to capture output
- `--reset` in `runner_params` – warm-reset after flash so board boots into new firmware

## Twister Command

```bat
west twister -p qcc748m_evk -T <test_path> ^
  --device-testing ^
  --hardware-map qapp\qcc74x\qcc74x_testing\hw_management\ci-hardware-map-win.yaml ^
  --extra-args="EXTRA_CONF_FILE=C:/Users/xupan/qcc/qapp/qcc74x/qcc74x_testing/hw_management/uart_edl_stub.conf" ^
  --extra-args="EXTRA_CFLAGS=-msmall-data-limit=0"
```

The `uart_edl_stub.conf` adds:
- `CONFIG_QCC74X_UART_EDL_TRIGGER=y` – lightweight EDL listener (auto-ignored if app has shell)
- `CONFIG_UART_INTERRUPT_DRIVEN=y` – required by the trigger
- `CONFIG_BOOT_DELAY=2000` – delays banner by 2s so twister can capture it

## Limitations

- **No automatic recovery from hang/crash**: If a test causes the board to hang or crash (no shell, no EDL trigger responding), the next test's pre-flash will fail. Manual intervention is needed (power cycle or hold BOOT button to enter EDL).
- **First-time bootstrap**: The very first flash requires the board to already have a firmware that can enter EDL (shell or uart_edl_trigger). If the board is bricked/empty, manually flash once using BOOT button + reset.
- This mode is **not the default** for regression testing. Enable it only when RTS/DTR lines are unavailable.