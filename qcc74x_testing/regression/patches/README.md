# Zephyr Patches for QCC74x Twister Testing

Patches in this directory must be applied to the `zephyr/` tree before
running `west twister` with QCC74x hardware.

## Apply a patch

Run from the **workspace root**:

**Windows (CMD)**

Original Twister patch:
```bat
git -C zephyr apply --ignore-whitespace ..\qapp\qcc74x\qcc74x_testing\regression\patches\twister_rts_dtr_fix.patch
```

Pytest serial fix patch:
```bat
git -C zephyr apply --ignore-whitespace ..\qapp\qcc74x\qcc74x_testing\regression\patches\twister_pytest_serial_fix.patch
```

**Linux / macOS**

Original Twister patch:
```sh
git -C zephyr apply --ignore-whitespace ../qapp/qcc74x/qcc74x_testing/regression/patches/twister_rts_dtr_fix.patch
```

Pytest serial fix patch:
```sh
git -C zephyr apply --ignore-whitespace ../qapp/qcc74x/qcc74x_testing/regression/patches/twister_pytest_serial_fix.patch
```


## Revert a patch

Run from the **workspace root**:

**Windows (CMD) / Linux / macOS**
```
git -C zephyr checkout scripts/pylib/twister/twisterlib/handlers.py
```

---

## Patch list

| File | Description |
|------|-------------|
| `twister_rts_dtr_fix.patch` | QCC74x EVK (`qcc743m_evk` / `qcc744m_evk` / `qcc748m_evk`): perform RTS/DTR reset pulse and deferred serial open after flashing, so the CH347 boot log is not missed. |
| `twister_pytest_serial_fix.patch` | QCC74x SPI test support: control RTS/DTR before serial open, preserve the boot banner, handle startup noise safely, and wait for SPI test reset/flash capture correctly. |

> `twister_pytest_serial_fix.patch` includes the Twister reset/serial changes from
> `twister_rts_dtr_fix.patch`; apply only one of these two patches.

---

## Hardware Jumper Connections

The automated test flow requires **CH347 RTS** to be connected to **CHIP_EN** (for reset control) and **GPIO2** to be connected to **GPIO2_OPTION** (for boot mode selection). The exact jumper locations vary between EVK revisions.

Below is the configuration for **QCC748M-EVK 3.1**:

| Connection | Jumper on QCC748M-EVK 3.1 |
|------------|---------------------------|
| CH347_RTS1 ↔ CHIP_EN | J4 Pin11 – Pin13 |
| GPIO2 ↔ GPIO2_OPTION | J3 Pin1 – Pin2 |

> **Note:** Different board revisions may use different jumper positions. The key requirement is that the CH347 RTS/DTR signals are routed to **CHIP_EN** and **GPIO2** on the target SoC.
