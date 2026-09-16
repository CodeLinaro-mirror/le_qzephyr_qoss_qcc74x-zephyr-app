# SPI L4 Shell Test

## Hardware wiring

Single-board mode: connect `GPIO31 (MOSI)` to `GPIO34 (MISO)` on the EVK.
The single-board overlay uses `SCLK=GPIO29`, `MOSI=GPIO31`, `MISO=GPIO34`,
and `SS=GPIO24`.

Dual-board mode:

```text
Master             Slave
SCLK (GPIO13)  ↔   SCLK (GPIO13)
MOSI (GPIO11)  ↔   MOSI (GPIO11)
MISO (GPIO10)  ↔   MISO (GPIO10)
SS   (GPIO12)  ↔   SS   (GPIO12)
GND            ↔   GND
```

## Run on Windows

The hardware map fixes the Twister/master board to `COM7`. The slave port is
provided to pytest through `SPI_SLAVE_PORT`; it is not part of the hardware map.
Run the commands below from the workspace root.

### Single-board test

Connect `GPIO31 (MOSI)` to `GPIO34 (MISO)` on one board. Set the slave port to
empty and run only the single-board scenario:

```bat
set "SPI_MASTER_PORT=COM7"
set "SPI_SLAVE_PORT="
call qapp\qcc74x\qcc74x_testing\regression\env.bat && west twister -T qapp\qcc74x\qcc74x_testing\tests\drivers\spi\pytest_shell -p qcc748m_evk --device-testing --hardware-map qapp\qcc74x\qcc74x_testing\tests\drivers\spi\pytest_shell\hardware-map-L4-win.yaml --ninja -O C:\tw\spi_L4_single --short-build-path --extra-args="EXTRA_CFLAGS=-msmall-data-limit=0" -s qcc7xx.drivers.spi.shell.single_board.polling
```

This runs the three single-board smoke tests and does not require a second
board.

### Dual-board test

Connect the two boards according to the wiring table above. Use `COM7` for the
master and `COM8` for the slave:

```bat
set "SPI_MASTER_PORT=COM7"
set "SPI_SLAVE_PORT=COM8"
call qapp\qcc74x\qcc74x_testing\regression\env.bat && west twister -T qapp\qcc74x\qcc74x_testing\tests\drivers\spi\pytest_shell -p qcc748m_evk --device-testing --hardware-map qapp\qcc74x\qcc74x_testing\tests\drivers\spi\pytest_shell\hardware-map-L4-win.yaml --ninja -O C:\tw\spi_L4 --short-build-path --extra-args="EXTRA_CFLAGS=-msmall-data-limit=0"
```

With `SPI_SLAVE_PORT` set, pytest flashes and opens the slave board. The full
run contains these four dual-board scenarios:

- `qcc7xx.drivers.spi.shell.dual_board.polling`
- `qcc7xx.drivers.spi.shell.dual_board.interrupt_sto`
- `qcc7xx.drivers.spi.shell.dual_board.dma_throughput_mosi_40mhz`
- `qcc7xx.drivers.spi.shell.dual_board.dma_throughput_miso_26mhz`

The full command also runs the single-board smoke scenario. Use `-s` with one
of the scenario names above when only one dual-board scenario is needed.

The default run is intentionally short. It covers a small single-board and
dual-board smoke set, verifies the interrupt-mode slave STO path, then measures
only these two DMA throughput points:

- MOSI at 40 MHz SCLK.
- MISO at 26 MHz SCLK, the stable read-direction limit on this setup.

The STO test prepares the slave for a longer frame, starts a shorter master
transfer with `-c` to keep CS asserted, and verifies that `slave_read` reports
the hardware timeout. It then verifies `qcom spi release`.

Each throughput test transfers four 64 KiB blocks in one transaction. The test
measures the transfer duration from the pytest serial controller and prints
the calculated reference rate in Mbps, for
example:

```text
MOSI 40 MHz throughput: ... rate=39.xx Mbps
MISO 26 MHz throughput: ... rate=25.xx Mbps ...
```

The reported rate is host-observed and informational; it includes shell and
UART command overhead, so it is not the raw SPI wire rate. No minimum
throughput threshold is used as a pytest failure condition. Transfer
completion and byte accounting are still checked.

The shell command group is:

```text
qcom spi <command>
```

For example:

```text
qcom spi status
qcom spi config -r master -m 0 -f 40000000
qcom spi transceive -p incr -l 32 -n 1
```
