# QCLI App

A minimal shell demo application for QCC74x/QCR373 platforms.

## Supported Boards

- `qcc748m_evk` — QCC748 EVK (AP core)
- `qcr373_evk/qcr373/ap` — QCR373 EVK (AP core)

## Build

```bash
# QCR373
west build -b qcr373_evk/qcr373/ap qapp/qcc74x/app/qcli_app \
    -d build/qcr373_qcli

# QCC748
west build -b qcc748m_evk qapp/qcc74x/app/qcli_app \
    -d build/qcc748_qcli
```

## QCR373 Prerequisites

Before building for QCR373, the following prebuilt files must be placed under
`modules/hal/qcom/qcc74x/zephyr/blobs/qcr373/`:

```
blobs/qcr373/
├── bin/
│   ├── boot2_qcr373_isp_release_v8.2.4.bin   # Boot2 bootloader image
│   └── flash_para.bin                          # Flash parameters for QConn_Flash_Cmd
└── tool/
    ├── QConn_Flash_Cmd-ubuntu                  # Flash tool (Linux)
    ├── QConn_Flash_Cmd-macos                   # Flash tool (macOS)
    ├── QConn_Flash_Cmd.exe                     # Flash tool (Windows)
    ├── QConn_Secure-ubuntu                     # Secure image tool (Linux)
    ├── QConn_Secure-macos                      # Secure image tool (macOS)
    └── QConn_Secure.exe                        # Secure image tool (Windows)
```

These files can be obtained from the HAL release:

- **Repository**: https://github.com/bouffalolab/hal_qualcomm
- **Branch**: `qcr373`
- **Release**: `v1.0.1`
