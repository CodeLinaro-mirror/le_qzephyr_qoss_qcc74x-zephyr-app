# CI Hardware Board Connections

Physical pin mapping and connection state for every CI board in
`qcc7xx_unikie_utils/hw_management/ci-hardware-map.yaml`.

> **Update this file whenever jumpers or external wires are changed!**

## General information

### qcc743m_evk family GPIO Pin Mux Table

| Pad name          | QSPI | SDIO | SDH  | SPI  | I2C (0/1) | UART (0/1)    | I2S  | PWM       | CAN (ISO 11898) | ADC       | DAC     | JTAG |
|-------------------|------|------|------|------|-----------|---------------|------|-----------|-----------------|-----------|---------|------|
| GPIO0             | –    | –    | –    | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH0P | TX/RX           | CH9       | –       | TMS  |
| GPIO1             | –    | –    | –    | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH0N | TX/RX           | CH8       | –       | TCK  |
| GPIO2             | –    | –    | –    | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH1P | TX/RX           | CH2       | –       | TDO  |
| GPIO3             | –    | –    | –    | MOSI | SDA       | RTS/CTS/TX/RX | DO   | CH3P/CH1N | TX/RX           | CH3       | –       | TDI  |
| GPIO10 (Reserved) | SCL  | DAT2 | DAT1 | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH1P | TX/RX           | CH7       | –       | TDO  |
| GPIO11 (Reserved) | CS   | DAT3 | DAT0 | MOSI | SDA       | RTS/CTS/TX/RX | DO   | CH3P/CH1N | TX/RX           | –         | –       | TDI  |
| GPIO12 (Reserved) | SDA0 | CMD  | CLK  | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH2P | TX/RX           | CH6       | –       | TMS  |
| GPIO13 (Reserved) | SDA1 | CLK  | CMD  | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH2N | TX/RX           | CH5       | –       | TCK  |
| GPIO14 (Reserved) | SDA2 | DAT0 | DAT3 | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH3P | TX/RX           | CH4       | A_PWM_P | TDO  |
| GPIO15 (Reserved) | SDA3 | DAT1 | DAT2 | MOSI | SDA       | RTS/CTS/TX/RX | DO   | CH3P/CH3N | TX/RX           | –         | A_PWM_N | TDI  |
| GPIO16            | –    | –    | –    | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH0P | TX/RX           | –         | –       | TMS  |
| GPIO17            | –    | –    | –    | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH0N | TX/RX           | –         | –       | TCK  |
| GPIO20            | –    | –    | –    | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH2P | TX/RX           | CH0/ACH0  | –       | TMS  |
| GPIO21            | –    | –    | –    | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH2N | TX/RX           | ACH1      | –       | TCK  |
| GPIO22            | –    | –    | –    | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH3P | TX/RX           | ACH2      | A_PWM_P | TDO  |
| GPIO27            | –    | –    | –    | MOSI | SDA       | RTS/CTS/TX/RX | DO   | CH3P/CH1N | TX/RX           | CH10/ACH4 | A_PWN_N | TDI  |
| GPIO28            | –    | –    | –    | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH2P | TX/RX           | CH11/ACH5 | A_PWM_P | TMS  |
| GPIO29            | –    | –    | –    | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH2N | TX/RX           | ACH6      | –       | TCK  |
| GPIO30            | –    | –    | –    | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH3P | TX/RX           | ACH7      | –       | TDO  |

Some pins are already in use for default configuration (like UART). Check device tree files for more information:

- `qcc74x_zephyr_unikie/boards/qcom/qcc743m_evk/qcc743m_evk-pinctrl.dtsi`
- `qcc74x_zephyr_unikie/boards/qcom/qcc743m_evk/qcc743m_evk.dts`

Prefer high numbered pins (GPIO16-GPIO30) than low numbered pins (GPIO0-GPIO3).

### qcc744m_evk family GPIO Pin Mux Table

| GPIO              | QSPI | SDIO | SDH  | SPI  | I2C (0/1) | UART (0/1)    | I2S  | PWM             | CAN (ISO 11898) | ADC     | DAC     | EMAC       | DVP          | DBI |
|-------------------|------|------|------|------|-----------|---------------|------|-----------------|-----------------|---------|---------|------------|--------------|-----|
| GPIO0             | –    | –    | –    | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH0P TX/RX | CH9             | –       | –       | CAM1_VSYNC | –            | TMS |
| GPIO1             | –    | –    | –    | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH0N TX/RX | CH8             | –       | –       | CAM1_HSYNC | –            | TCK |
| GPIO2             | –    | –    | –    | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH1P TX/RX | CH2             | –       | –       | –          | –            | TDO |
| GPIO3             | –    | –    | –    | MOSI | SDA       | RTS/CTS/TX/RX | DO   | CH3P/CH1N TX/RX | CH3             | –       | –       | CAM1_DAT0  | –            | TDI |
| GPIO10 (Reserved) | SCL  | DAT2 | DAT1 | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH1P TX/RX | CH7             | –       | –       | CAM1_DAT1  | B_DB2/C_SDA0 | TDO |
| GPIO11 (Reserved) | CS   | DAT3 | DAT0 | MOSI | SDA       | RTS/CTS/TX/RX | DO   | CH3P/CH1N TX/RX | –               | –       | –       | CAM1_DAT2  | B_DB3/C_DCn  | TDI |
| GPIO12 (Reserved) | SDA0 | CMD  | CLK  | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH2P TX/RX | CH6             | –       | –       | CAM1_DAT3  | B_DB4/C_SCL  | TMS |
| GPIO13 (Reserved) | SDA1 | CLK  | CMD  | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH2N TX/RX | CH5             | –       | –       | CAM1_CLK   | B_DB5/C_CSn  | TCK |
| GPIO14 (Reserved) | SDA2 | DAT0 | DAT3 | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH3P TX/RX | CH4             | A_PWM_P | –       | CAM1_DAT4  | B_DB6/C_SDA0 | TDO |
| GPIO15 (Reserved) | SDA3 | DAT1 | DAT2 | MOSI | SDA       | RTS/CTS/TX/RX | DO   | CH3P/CH3N TX/RX | –               | A_PWM_N | –       | CAM1_DAT5  | B_DB7/C_DCn  | TDI |
| GPIO16            | –    | –    | –    | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH0P TX/RX | –               | –       | –       | CAM1_DAT6  | –            | TMS |
| GPIO17            | –    | –    | –    | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH0N TX/RX | –               | –       | –       | CAM1_DAT7  | –            | TCK |
| GPIO18            | –    | –    | –    | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH1P TX/RX | –               | –       | –       | –          | –            | TDO |
| GPIO19            | –    | –    | –    | MOSI | SDA       | RTS/CTS/TX/RX | DO   | CH3P/CH1N TX/RX | CH1             | –       | –       | –          | –            | TDI |
| GPIO20            | –    | –    | –    | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH2P TX/RX | CH0/ACH0        | –       | –       | –          | –            | TMS |
| GPIO21            | –    | –    | –    | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH2N TX/RX | ACH1            | –       | –       | –          | –            | TCK |
| GPIO22            | –    | –    | –    | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH3P TX/RX | ACH2            | A_PWM_P | –       | –          | –            | TDO |
| GPIO23            | –    | –    | –    | MOSI | SDA       | RTS/CTS/TX/RX | DO   | CH3P/CH3N TX/RX | ACH3            | A_PWN_N | –       | –          | –            | TDI |
| GPIO24            | –    | –    | –    | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH0P TX/RX | –               | –       | –       | CAM0_DAT0  | –            | TMS |
| GPIO25            | –    | –    | –    | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH0N TX/RX | –               | –       | REF_CLK | CAM0_DAT1  | –            | TCK |
| GPIO26            | –    | –    | –    | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH1P TX/RX | –               | –       | TXD[0]  | CAM0_DAT2  | –            | TDO |
| GPIO27            | –    | –    | –    | MOSI | SDA       | RTS/CTS/TX/RX | DO   | CH3P/CH1N TX/RX | CH10/ACH4       | A_PWN_N | TXD[1]  | CAM0_DAT3  | –            | TDI |
| GPIO28            | –    | –    | –    | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH2P TX/RX | CH11/ACH5       | A_PWM_P | RXD[0]  | CAM0_HSYNC | –            | TMS |
| GPIO29            | –    | –    | –    | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH2N TX/RX | ACH6            | –       | RXD[1]  | CAM0_VSYNC | –            | TCK |
| GPIO30            | –    | –    | –    | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH3P TX/RX | ACH7            | –       | RXERR   | CAM0_CLK   | –            | TDO |
| GPIO31            | –    | –    | –    | MOSI | SDA       | RTS/CTS/TX/RX | DO   | CH3P/CH3N TX/RX | –               | –       | TX_EN   | CAM0_DAT4  | –            | TDI |
| GPIO32            | –    | –    | –    | CS   | SCL       | RTS/CTS/TX/RX | BCLK | CH0P/CH0P TX/RX | –               | –       | RX_DV   | CAM0_DAT5  | –            | TMS |
| GPIO33            | –    | –    | –    | CLK  | SDA       | RTS/CTS/TX/RX | FS   | CH1P/CH0N TX/RX | –               | –       | MDC     | CAM0_DAT6  | –            | TCK |
| GPIO34            | –    | –    | –    | MISO | SCL       | RTS/CTS/TX/RX | DI   | CH2P/CH1P TX/RX | –               | –       | MDIO    | CAM0_DAT7  | –            | TDO |

Some pins are already in use for default configuration (like UART). Check device tree files for more information:

- `qcc74x_zephyr_unikie/boards/qcom/qcc744m_evk/qcc743m_evk-pinctrl.dtsi`
- `qcc74x_zephyr_unikie/boards/qcom/qcc744m_evk/qcc743m_evk.dts`

Prefer high numbered pins (GPIO16-GPIO34) than low numbered pins (GPIO0-GPIO3).

### qcc748m_evk family GPIO Pin Mux Table

The qcc748m_evk GPIO pin mux table is identical to the qcc744m_evk family table above.

Some pins are already in use for default configuration (like UART). Check device tree files for more information:

- `qcc74x_zephyr_unikie/boards/qcom/qcc748m_evk/qcc748m_evk-pinctrl.dtsi`
- `qcc74x_zephyr_unikie/boards/qcom/qcc748m_evk/qcc748m_evk.dts`

Prefer high numbered pins (GPIO16-GPIO34) than low numbered pins (GPIO0-GPIO3).


### J3 board jumper - GPIO/JTAG/UART Jumper

J3 jumper is physical jumper on qcc743m_evk and qcc744m_evk. Jumper layout is common for both boards: qcc743m_evk and qcc744m_evk.
Table below only presents all possible connections on that jumper.
Detailed information which jumpers are connected for each board is specific for the board and described for each board.

| Pins  | Left   | Right      | Function          |
|-------|--------|------------|-------------------|
| 1-2   | GPIO2  | CH347_DTR1 | DTR1 Pin Select   |
| 3-4   | GPIO2  | CH347_TDO  | JTAG TDO          |
| 5-6   | GPIO1  | CH347_TCK  | JTAG TCK          |
| 7-8   | GPIO3  | CH347_TDI  | JTAG TDI          |
| 9-10  | GPIO0  | CH347_TMS  | JTAG TMS          |
| 11-12 | GPIO22 | CH347_TXD1 | UART Console RX   |
| 13-14 | GPIO21 | CH347_RXD1 | UART Console TX   |
| 15-16 | GPIO28 | CH347_CTS1 | UART Flow Control |
| 17-18 | GPIO29 | CH347_RTS1 | UART Flow Control |

### J4 - Power/Reset/GPIO29 Jumper

J4 jumper is physical jumper on qcc743m_evk and qcc744m_evk. Jumper layout is common for both boards: qcc743m_evk and qcc744m_evk.
Table below only presents all possible connections on that jumper.
Detailed information which jumpers are connected for each board is specific for the board and described for each board.

| Pins  | Left       | Right      | Function |
|-------|------------|------------|----------|
| 1-2   | VDD33      | VDD_3V3_TP | -        |
| 3-5   | VDD_3V3_TP | VDDIO2     | -        |
| 4-6   | VDD_3V3_TP | VDDIO1     | -        |
| 7-8   | VDD_1V8_TP | VDD_1V8_TP | -        |
| 9-10  | RESET_KEY  | VDD_1V8    | -        |
| 11-12 | CHIP_EN    | VDD        | -        |
| 13-14 | GPIO29     | VDD_3V3    | -        |
| 15-16 | -          | -          | -        |
| 17-18 | VDD_5V0    | USB_5V0    | -        |

---

## Board: `qcc744m_evk_1_1`

| Field        | Value                                     |
|:-------------|-------------------------------------------|
| **ID**       | `qcc744m_evk_1_1`                         |
| **Platform** | `qcc744m_evk`                             |
| **Serial**   | `/dev/qcc744m_evk_1_1`                    |

---

### Board GPIO PINs

#### IO_L - Left Column (pins 1-24)

| Pin | Signal     | Notes                                            |
|-----|------------|--------------------------------------------------|
| 1   | GPIO0      | J3 pin 9                                         |
| 2   | GPIO1      | J3 pin 5                                         |
| 3   | GPIO2      | J3 pin 1 and J3 pin 3                            |
| 4   | GPIO3      | J3 pin 7                                         |
| 5   | GPIO15     |                                                  |
| 6   | GPIO14     |                                                  |
| 7   | GPIO13     |                                                  |
| 8   | GPIO12     |                                                  |
| 9   | GPIO11     |                                                  |
| 10  | GPIO10     |                                                  |
| 11  | GPIO4      | Reserved for NOR Flash, do not use it            |
| 12  | GPIO5      | Reserved for NOR Flash, do not use it            |
| 13  | GPIO6      | Reserved for NOR Flash, do not use it            |
| 14  | GPIO7      | Reserved for NOR Flash, do not use it            |
| 15  | GPIO8      | Reserved for NOR Flash, do not use it            |
| 16  | GPIO9      | Reserved for NOR Flash, do not use it            |
| 17  | GPIO16     | Connected to GPIO20                              |
| 18  | GPIO17     |                                                  |
| 19  | GPIO18     | I2C1 SCL for LIS2DW12                            |
| 20  | GPIO19     | I2C1 SDA for LIS2DW12                            |
| 21  | CHIP_EN    |                                                  |
| 22  | USB_DM     |                                                  |
| 23  | USB_DP     |                                                  |
| 24  | GND        |                                                  |

#### IO_R - Right Column (pins 25-48)

| Pin | Signal     | Notes                           |
|:----|:-----------|:--------------------------------|
| 25  | GND        |                                 |
| 26  | VDD_5V0    |                                 |
| 27  | GND        |                                 |
| 28  | VDD_3V3_TP |                                 |
| 29  | VDD_1V8_TP |                                 |
| 30  | GND        |                                 |
| 31  | VDD_3V3    |                                 |
| 32  | VDD_1V8    |                                 |
| 33  | GND        |                                 |
| 34  | GPIO20     | Connected to GPIO16             |
| 35  | GPIO21     | J3 pin 13, Reserved for UART RX |
| 36  | GPIO22     | J3 pin 11, Reserved for UART TX |
| 37  | GPIO23     |                                 |
| 38  | GPIO24     |                                 |
| 39  | GPIO25     |                                 |
| 40  | GPIO26     |                                 |
| 41  | GPIO33     |                                 |
| 42  | GPIO27     | Connected to GPIO28             |
| 43  | GPIO32     |                                 |
| 44  | GPIO30     | Connected to GPIO29             |
| 45  | GPIO31     | Connected to GPIO34             |
| 46  | GPIO34     | Connected to GPIO31             |
| 47  | GPIO29     | J3 pin 17  Connected to GPIO30  |
| 48  | GPIO28     | J3 pin 15, Connected to GPIO27  |

---

### J3 Jumper

Only wired pins are listed; unlisted pairs are assumed open.

| J3 Pins | Left (lower number) | Right (higer number) | State                         |
|:--------|:--------------------|:---------------------|:------------------------------|
| 1-2     | GPIO2               | CH347_DTR1           | CONNECTED                     |
| 11-12   | GPIO22              | CH347_TXD1           | CONNECTED                     |
| 13-14   | GPIO21              | CH347_RXD1           | CONNECTED                     |
| 18      | GPIO29              | N/A                  | CONNECTED, *(see cross-conn)* |

---

### J4 Jumper

Only wired pins are listed; unlisted pairs are assumed open.

| J4 Pins | Left Signal | Right Signal | State         | Purpose             |
|:--------|:------------|:-------------|:--------------|:--------------------|
| 1-2     | VDD33       | VDD_3V3_TP   | CONNECTED     | 3.3 V power         |
| 3-5     | VDD_3V3_TP  | VDD_3V3_TP   | CONNECTED     | 3.3 V connections   |
| 4-6     | VDD_3V3_TP  | VDD_3V3_TP   | CONNECTED     | 3.3 V connections   |
| 9-11    | RESET_KEY   | CHIP_EN      | CONNECTED     | Reset / chip enable |
| 12-14   | VDD         | VDD_3V3      | CONNECTED     | Main power          |
| 17-18   | VDD_5V0     | USB_5V0      | CONNECTED     | USB 5 V power       |

### Cross-conn

J3 pin 18 and J4 pin(s) 9 and 11. All three pins are wired together. Connection created for west twister execution.

---

## Board: `qcc744m_evk_2_1`

| Field        | Value                                     |
|:-------------|-------------------------------------------|
| **ID**       | `qcc744m_evk_2_1`                         |
| **Platform** | `qcc744m_evk`                             |
| **Serial**   | `/dev/qcc744m_evk_2_1`                    |

---

### Board GPIO PINs

#### IO_L - Left Column (pins 1-24)

| Pin | Signal  | Notes                                 |
|-----|---------|---------------------------------------|
| 1   | GPIO0   | J3 pin 9                              |
| 2   | GPIO1   | J3 pin 5                              |
| 3   | GPIO2   | J3 pin 1 and J3 pin 3                 |
| 4   | GPIO3   | J3 pin 7                              |
| 5   | GPIO15  |                                       |
| 6   | GPIO14  |                                       |
| 7   | GPIO13  |                                       |
| 8   | GPIO12  |                                       |
| 9   | GPIO11  |                                       |
| 10  | GPIO10  |                                       |
| 11  | GPIO4   | Reserved for NOR Flash, do not use it |
| 12  | GPIO5   | Reserved for NOR Flash, do not use it |
| 13  | GPIO6   | Reserved for NOR Flash, do not use it |
| 14  | GPIO7   | Reserved for NOR Flash, do not use it |
| 15  | GPIO8   | Reserved for NOR Flash, do not use it |
| 16  | GPIO9   | Reserved for NOR Flash, do not use it |
| 17  | GPIO16  | Connected to GPIO20                   |
| 18  | GPIO17  |                                       |
| 19  | GPIO18  |                                       |
| 20  | GPIO19  |                                       |
| 21  | CHIP_EN |                                       |
| 22  | USB_DM  |                                       |
| 23  | USB_DP  |                                       |
| 24  | GND     |                                       |

#### IO_R - Right Column (pins 25-48)

| Pin | Signal     | Notes                           |
|:----|:-----------|:--------------------------------|
| 25  | GND        |                                 |
| 26  | VDD_5V0    |                                 |
| 27  | GND        |                                 |
| 28  | VDD_3V3_TP |                                 |
| 29  | VDD_1V8_TP |                                 |
| 30  | GND        |                                 |
| 31  | VDD_3V3    |                                 |
| 32  | VDD_1V8    |                                 |
| 33  | GND        |                                 |
| 34  | GPIO20     | Connected to GPIO16             |
| 35  | GPIO21     | J3 pin 13, Reserved for UART RX |
| 36  | GPIO22     | J3 pin 11, Reserved for UART TX |
| 37  | GPIO23     |                                 |
| 38  | GPIO24     |                                 |
| 39  | GPIO25     |                                 |
| 40  | GPIO26     |                                 |
| 41  | GPIO33     |                                 |
| 42  | GPIO27     | Connected to GPIO28             |
| 43  | GPIO32     |                                 |
| 44  | GPIO30     | Connected to GPIO29             |
| 45  | GPIO31     | Connected to GPIO34             |
| 46  | GPIO34     | Connected to GPIO31             |
| 47  | GPIO29     | J3 pin 17  Connected to GPIO30  |
| 48  | GPIO28     | J3 pin 15, Connected to GPIO27  |

---

### J3 Jumper

Only wired pins are listed; unlisted pairs are assumed open.

| J3 Pins | Left (lower number) | Right (higer number) | State                         |
|:--------|:--------------------|:---------------------|:------------------------------|
| 1-2     | GPIO2               | CH347_DTR1           | CONNECTED                     |
| 11-12   | GPIO22              | CH347_TXD1           | CONNECTED                     |
| 13-14   | GPIO21              | CH347_RXD1           | CONNECTED                     |
| 18      | GPIO29              | N/A                  | CONNECTED, *(see cross-conn)* |

---

### J4 Jumper

Only wired pins are listed; unlisted pairs are assumed open.

| J4 Pins | Left Signal | Right Signal | State         | Purpose             |
|:--------|:------------|:-------------|:--------------|:--------------------|
| 1-2     | VDD33       | VDD_3V3_TP   | CONNECTED     | 3.3 V power         |
| 3-5     | VDD_3V3_TP  | VDD_3V3_TP   | CONNECTED     | 3.3 V connections   |
| 4-6     | VDD_3V3_TP  | VDD_3V3_TP   | CONNECTED     | 3.3 V connections   |
| 9-11    | RESET_KEY   | CHIP_EN      | CONNECTED     | Reset / chip enable |
| 12-14   | VDD         | VDD_3V3      | CONNECTED     | Main power          |
| 17-18   | VDD_5V0     | USB_5V0      | CONNECTED     | USB 5 V power       |

### Cross-conn

J3 pin 18 and J4 pin(s) 9 and 11. All three pins are wired together. Connection created for west twister execution.

---

## Board: `qcc748m_evk_1_2`

| Field        | Value                  |
|:-------------|------------------------|
| **ID**       | `qcc748m_evk_1_2`      |
| **Platform** | `qcc748m_evk`          |
| **Serial**   | `/dev/qcc748m_evk_1_2` |

---

### Board GPIO PINs

#### IO_L - Left Column (pins 1-24)

| Pin | Signal  | Notes                                 |
|-----|---------|---------------------------------------|
| 1   | GPIO0   | J3 pin 9                              |
| 2   | GPIO1   | J3 pin 5                              |
| 3   | GPIO2   | J3 pin 1 and J3 pin 3                 |
| 4   | GPIO3   | J3 pin 7                              |
| 5   | GPIO15  |                                       |
| 6   | GPIO14  |                                       |
| 7   | GPIO13  |                                       |
| 8   | GPIO12  |                                       |
| 9   | GPIO11  |                                       |
| 10  | GPIO10  |                                       |
| 11  | GPIO4   | Reserved for NOR Flash, do not use it |
| 12  | GPIO5   | Reserved for NOR Flash, do not use it |
| 13  | GPIO6   | Reserved for NOR Flash, do not use it |
| 14  | GPIO7   | Reserved for NOR Flash, do not use it |
| 15  | GPIO8   | Reserved for NOR Flash, do not use it |
| 16  | GPIO9   | Reserved for NOR Flash, do not use it |
| 17  | GPIO16  |                                       |
| 18  | GPIO17  |                                       |
| 19  | GPIO18  |                                       |
| 20  | GPIO19  |                                       |
| 21  | CHIP_EN |                                       |
| 22  | USB_DM  |                                       |
| 23  | USB_DP  |                                       |
| 24  | GND     |                                       |

#### IO_R - Right Column (pins 25-48)

| Pin | Signal     | Notes                           |
|:----|:-----------|:--------------------------------|
| 25  | GND        |                                 |
| 26  | VDD_5V0    |                                 |
| 27  | GND        |                                 |
| 28  | VDD_3V3_TP |                                 |
| 29  | VDD_1V8_TP |                                 |
| 30  | GND        |                                 |
| 31  | VDD_3V3    |                                 |
| 32  | VDD_1V8    |                                 |
| 33  | GND        |                                 |
| 34  | GPIO20     |                                 |
| 35  | GPIO21     | J3 pin 13, Reserved for UART RX |
| 36  | GPIO22     | J3 pin 11, Reserved for UART TX |
| 37  | GPIO23     |                                 |
| 38  | GPIO24     |                                 |
| 39  | GPIO25     |                                 |
| 40  | GPIO26     |                                 |
| 41  | GPIO33     |                                 |
| 42  | GPIO27     |                                 |
| 43  | GPIO32     |                                 |
| 44  | GPIO30     |                                 |
| 45  | GPIO31     |                                 |
| 46  | GPIO34     |                                 |
| 47  | GPIO29     | J3 pin 17                       |
| 48  | GPIO28     | J3 pin 15                       |

---

### J3 Jumper

Only wired pins are listed; unlisted pairs are assumed open.

| J3 Pins | Left (lower number) | Right (higer number) | State                         |
|:--------|:--------------------|:---------------------|:------------------------------|
| 1-2     | GPIO2               | CH347_DTR1           | CONNECTED                     |
| 11-12   | GPIO22              | CH347_TXD1           | CONNECTED                     |
| 13-14   | GPIO21              | CH347_RXD1           | CONNECTED                     |
| 18      | GPIO29              | N/A                  | CONNECTED, *(see cross-conn)* |

---

### J4 Jumper

Only wired pins are listed; unlisted pairs are assumed open.

| J4 Pins | Left Signal | Right Signal | State         | Purpose             |
|:--------|:------------|:-------------|:--------------|:--------------------|
| 1-2     | VDD33       | VDD_3V3_TP   | CONNECTED     | 3.3 V power         |
| 3-5     | VDD_3V3_TP  | VDD_3V3_TP   | CONNECTED     | 3.3 V connections   |
| 4-6     | VDD_3V3_TP  | VDD_3V3_TP   | CONNECTED     | 3.3 V connections   |
| 9-11    | RESET_KEY   | CHIP_EN      | CONNECTED     | Reset / chip enable |
| 12-14   | VDD         | VDD_3V3      | CONNECTED     | Main power          |
| 17-18   | VDD_5V0     | USB_5V0      | CONNECTED     | USB 5 V power       |

### Cross-conn

J3 pin 18 and J4 pin(s) 9 and 11. All three pins are wired together. Connection created for west twister execution.

---

## Board: `qcc743m_evk_2_2`

| Field        | Value                  |
|:-------------|------------------------|
| **ID**       | `qcc743m_evk_2_2`      |
| **Platform** | `qcc743m_evk`          |
| **Serial**   | `/dev/qcc743m_evk_2_2` |

---

### Board GPIO PINs

#### IO_L - Left Column (pins 1-16)

| Pin | Signal  | Notes                               |
|-----|---------|-------------------------------------|
| 1   | GPIO0   | J3 pin 9                            |
| 2   | GPIO1   | J3 pin 5                            |
| 3   | GPIO2   | J3 pin 1 and J3 pin 3               |
| 4   | GPIO3   | J3 pin 7                            |
| 5   | GPIO12  | Reserved (SDIO/QSPI), do not use it |
| 6   | GPIO11  | Reserved (SDIO/QSPI), do not use it |
| 7   | GPIO10  | Reserved (SDIO/QSPI), do not use it |
| 8   | GPIO14  | Reserved (SDIO/QSPI), do not use it |
| 9   | GPIO13  | Reserved (SDIO/QSPI), do not use it |
| 10  | GPIO15  | Reserved (SDIO/QSPI), do not use it |
| 11  | GPIO17  |                                     |
| 12  | GPIO16  | Connected to GPIO20                 |
| 13  | CHIP_EN |                                     |
| 14  | USB_DM  |                                     |
| 15  | USB_DP  |                                     |
| 16  | GND     |                                     |

#### IO_R - Right Column (pins 17-32)


| Pin | Signal     | Notes                           |
|:----|:-----------|:--------------------------------|
| 17  | GND        |                                 |
| 18  | VDD_5V0    |                                 |
| 19  | GND        |                                 |
| 20  | VDD_3V3_TP |                                 |
| 21  | VDD_1V8_TP |                                 |
| 22  | GND        |                                 |
| 23  | VDD_3V3    |                                 |
| 24  | VDD_1V8    |                                 |
| 25  | GND        |                                 |
| 26  | GPIO20     | Connected to GPIO16             |
| 27  | GPIO21     | J3 pin 13, Reserved for UART TX |
| 28  | GPIO22     | J3 pin 11, Reserved for UART RX |
| 29  | GPIO28     | J3 pin 15, Connected to GPIO27  |
| 30  | GPIO29     | J3 pin 17, Connected to GPIO30  |
| 31  | GPIO30     | Connected to GPIO29             |
| 32  | GPIO27     | Connected to GPIO28             |

---

### J3 Jumper

Only wired pins are listed; unlisted pairs are assumed open.

| J3 Pins | Left (lower number) | Right (higer number) | State                         |
|:--------|:--------------------|:---------------------|:------------------------------|
| 1-2     | GPIO2               | CH347_DTR1           | CONNECTED                     |
| 11-12   | GPIO22              | CH347_TXD1           | CONNECTED                     |
| 13-14   | GPIO21              | CH347_RXD1           | CONNECTED                     |
| 18      | GPIO29              | N/A                  | CONNECTED, *(see cross-conn)* |

---

### J4 Jumper

Only wired pins are listed; unlisted pairs are assumed open.

| J4 Pins | Left Signal | Right Signal | State         | Purpose             |
|:--------|:------------|:-------------|:--------------|:--------------------|
| 1-2     | VDD33       | VDD_3V3_TP   | CONNECTED     | 3.3 V power         |
| 3-5     | VDD_3V3_TP  | VDD_3V3_TP   | CONNECTED     | 3.3 V connections   |
| 4-6     | VDD_3V3_TP  | VDD_3V3_TP   | CONNECTED     | 3.3 V connections   |
| 9-11    | RESET_KEY   | CHIP_EN      | CONNECTED     | Reset / chip enable |
| 12-14   | VDD         | VDD_3V3      | CONNECTED     | Main power          |
| 17-18   | VDD_5V0     | USB_5V0      | CONNECTED     | USB 5 V power       |

### Cross-conn

J3 pin 18 and J4 pin(s) 9 and 11. All three pins are wired together. Connection created for west twister execution.

---