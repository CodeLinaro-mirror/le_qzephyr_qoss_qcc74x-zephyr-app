/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/ztest.h>

#include <zephyr/dt-bindings/pinctrl/qc7xx-pinctrl-common.h>
#include <zephyr/dt-bindings/pinctrl/qc7xx-qcc74x-pinctrl.h>

/* pin configuration for test device */
#define TEST_DEVICE DT_NODELABEL(test_device)
PINCTRL_DT_DEV_CONFIG_DECLARE(TEST_DEVICE);
static const struct pinctrl_dev_config *pcfg = PINCTRL_DT_DEV_CONFIG_GET(TEST_DEVICE);

/* Extract function code from V44 pinmux value (bits[4:0] of func_cfg = bits[4:0] of pinmux). */
#define V44_GET_FUNC(pinmux)  QC7XX_PIN_GET_FUNC(pinmux)
/* Extract bias from pincfg (bits[5:4]). */
#define V44_GET_BIAS(pincfg)  (((pincfg) >> QC7XX_PIN_BIAS_SHIFT) & QC7XX_PIN_BIAS_MASK)

ZTEST(pinctrl_qcom, test_dt_extract)
{
	const struct pinctrl_state *scfg;
	pinctrl_soc_pin_t pin;
	unsigned idx = 0;

	zassert_equal(pcfg->state_cnt, 1U);
	scfg = &pcfg->states[0];
	zassert_equal(scfg->id, PINCTRL_STATE_DEFAULT);

	/* For DT definitions, refer to overlay files in the test "board" directory.
	 * All assertions use V44 extraction macros on pin.pinmux and pin.pincfg.
	 */

	/* Group1: GPIO30=I2C0_SCL, GPIO29=I2C0_SDA */
	pin = scfg->pins[idx++];
	zassert_equal(QC7XX_PIN_GET_NUM(pin.pinmux), 30U);
	zassert_equal(V44_GET_FUNC(pin.pinmux), GPIO_FUN_I2C0);
	zassert_equal(QC7XX_PIN_GET_IR_FLAG(pin.pinmux), 0U);

	pin = scfg->pins[idx++];
	zassert_equal(QC7XX_PIN_GET_NUM(pin.pinmux), 29U);
	zassert_equal(V44_GET_FUNC(pin.pinmux), GPIO_FUN_I2C0);

	/* Group2: GPIO22=UART0_RXD, GPIO21=UART0_TXD — bias-pull-up in pincfg */
	pin = scfg->pins[idx++];
	zassert_equal(QC7XX_PIN_GET_NUM(pin.pinmux), 22U);
	zassert_equal(V44_GET_FUNC(pin.pinmux), GPIO_FUN_UART);
	zassert_equal(QC7XX_PIN_GET_UART_SIG(pin.pinmux),
		      (uint32_t)GPIO_UART_FUNC_UART0_RXD);
	zassert_equal(V44_GET_BIAS(pin.pincfg), QC7XX_PULL_UP);

	pin = scfg->pins[idx++];
	zassert_equal(QC7XX_PIN_GET_NUM(pin.pinmux), 21U);
	zassert_equal(V44_GET_FUNC(pin.pinmux), GPIO_FUN_UART);
	zassert_equal(QC7XX_PIN_GET_UART_SIG(pin.pinmux),
		      (uint32_t)GPIO_UART_FUNC_UART0_TXD);
	zassert_equal(V44_GET_BIAS(pin.pincfg), QC7XX_PULL_UP);

	/* Group3: GPIO29=UART1_RXD, GPIO28=UART1_TXD — bias-pull-down in pincfg */
	pin = scfg->pins[idx++];
	zassert_equal(QC7XX_PIN_GET_NUM(pin.pinmux), 29U);
	zassert_equal(V44_GET_FUNC(pin.pinmux), GPIO_FUN_UART);
	zassert_equal(QC7XX_PIN_GET_UART_SIG(pin.pinmux),
		      (uint32_t)GPIO_UART_FUNC_UART1_RXD);
	zassert_equal(V44_GET_BIAS(pin.pincfg), QC7XX_PULL_DOWN);

	pin = scfg->pins[idx++];
	zassert_equal(QC7XX_PIN_GET_NUM(pin.pinmux), 28U);
	zassert_equal(V44_GET_FUNC(pin.pinmux), GPIO_FUN_UART);
	zassert_equal(QC7XX_PIN_GET_UART_SIG(pin.pinmux),
		      (uint32_t)GPIO_UART_FUNC_UART1_TXD);
	zassert_equal(V44_GET_BIAS(pin.pincfg), QC7XX_PULL_DOWN);

	/* Group4: GPIO2=ADC_CH2 — analog function */
	pin = scfg->pins[idx++];
	zassert_equal(QC7XX_PIN_GET_NUM(pin.pinmux), 2U);
	zassert_equal(V44_GET_FUNC(pin.pinmux), GPIO_FUN_ANALOG);

	/* Group5: GPIO9 as GPIO — IR receive flag set in bits[28:24] of pinmux */
	pin = scfg->pins[idx++];
	zassert_equal(QC7XX_PIN_GET_NUM(pin.pinmux), 9U);
	zassert_equal(V44_GET_FUNC(pin.pinmux), GPIO_FUN_GPIO);
	zassert_true(QC7XX_PIN_GET_IR_FLAG(pin.pinmux) != 0U);

	/* Group6: GPIO1=PWM0 — input-enable and output-enable set in pincfg */
	pin = scfg->pins[idx++];
	zassert_equal(QC7XX_PIN_GET_NUM(pin.pinmux), 1U);
	zassert_equal(V44_GET_FUNC(pin.pinmux), GPIO_FUN_PWM0);
	zassert_true(pin.pincfg & (QC7XX_PIN_IN_EN << QC7XX_PIN_IN_EN_SHIFT));
	zassert_true(pin.pincfg & (QC7XX_PIN_OUT_EN << QC7XX_PIN_OUT_EN_SHIFT));

	/* Group7: GPIO1=PWM0 — output-high set in pincfg */
	pin = scfg->pins[idx++];
	zassert_equal(QC7XX_PIN_GET_NUM(pin.pinmux), 1U);
	zassert_equal(V44_GET_FUNC(pin.pinmux), GPIO_FUN_PWM0);
	zassert_true(pin.pincfg & (QC7XX_PIN_OUT_HIGH << QC7XX_PIN_OUT_SHIFT));

	zassert_equal(scfg->pin_cnt, idx);
}

ZTEST_SUITE(pinctrl_qcom, NULL, NULL, NULL, NULL, NULL);
