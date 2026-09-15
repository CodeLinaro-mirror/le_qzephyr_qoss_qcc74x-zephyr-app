# SPI L0 Loopback Test

Run from the repository root.

## Hardware wiring

Use one QCC748M EVK. Connect `GPIO31 (MOSI)` to `GPIO34 (MISO)` for the
single-board loopback. No external SPI slave is required.

```bat
call qapp\qcc74x\qcc74x_testing\regression\env.bat && west twister -T qapp\qcc74x\qcc74x_testing\tests\drivers\spi\spi_loopback -p qcc748m_evk --device-testing --hardware-map qapp\qcc74x\qcc74x_testing\tests\drivers\spi\hardware-map-win.yaml --ninja -O C:\tw\spi_L0 --short-build-path --extra-args="EXTRA_CFLAGS=-msmall-data-limit=0"
```

The hardware map must match the board serial port. The test uses the single-board SPI loopback connection.
