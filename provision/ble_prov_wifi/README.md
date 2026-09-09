# BLE-assisted Wi-Fi provisioning

This application provides the QCC74x BLE provisioning path used by the
existing mobile client. It preserves the qcc74xsync wire contract while using
Zephyr Bluetooth, Wi-Fi management, kernel thread, message queue, semaphore,
and memory APIs.

## Source layout

The source is split by responsibility to remain traceable to the FreeRTOS
reference implementation:

- `qcc74xsync_ble.c/.h`: BLE GATT service, connection lifecycle, ATT MTU,
  Read/Notify transport, and the Zephyr protocol worker.
- `qcc74xsync_ble_app.c/.h`: Wi-Fi scan/connect/status adapter and application
  command handling.
- `transfer.c/.h`: outer frame, fragmentation, sequence, ACK, timeout, and
  reassembly state machine.
- `payload.c/.h`: command/data payload encoding and decoding.
- `encrypt_layer.c/.h`: qcc74xsync compatibility framing. The current layer
  does not perform actual payload encryption.
- `pro_config.h`: protocol buffer and timeout configuration.

## Build

From the west workspace root:

```powershell
west build -p always -b qcc748m_evk `
  qapp/qcc74x/provision/ble_prov_wifi `
  -d br_bt_ble_prov_wifi `
  -- -DDTC=C:\path\to\working\dtc.exe
```

If the local WinGet/MSYS2 DTC is blocked by the Windows environment, use the
workspace DTC fallback documented in the workspace AGENTS.md and record that
the fallback was used.

## Client references

- FreeRTOS reference project and APK location:
  [examples/wifi/sta/smartconfig_ble/qcc74xsync](https://git.codelinaro.org/clo/qcc7xx/QCCSDK-QCC74x/-/tree/master/examples/wifi/sta/smartconfig_ble/qcc74xsync?ref_type=heads)

The linked project is the reference source and artifact location used for
compatibility analysis and validation. The APK is not stored in this
repository.

## Scope notes

The current feature covers basic station-mode provisioning: BLE discovery and
session setup, Wi-Fi scan, credential transfer, Wi-Fi connection, status
reporting, and session recovery. CoEx changes and an APK security redesign are
outside this migration.
