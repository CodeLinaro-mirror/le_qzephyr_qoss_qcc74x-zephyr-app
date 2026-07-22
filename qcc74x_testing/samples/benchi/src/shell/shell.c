/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/shell/shell.h>

#include <bshg_zbus_helper.h>
#include <bshg_spi.h>
#include <stress.h>

LOG_MODULE_REGISTER(shell);

/******************************************************************************
 *
 * TODO
 *
 ******************************************************************************/

int TODO(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "'%s' not implemented yet", argv[0]);
	return 0;
}

/******************************************************************************
 *
 * "bshg trigger" commands
 *
 ******************************************************************************/
extern void trigger_led_on();
extern void trigger_led_off();
extern void BSHG_startTracing(void);

result_t trigger_message(const json_msg *request_data, uint32_t timeout_ms, const json_msg *reply);

int bshg_trigger_tracing_init(const struct shell *sh, size_t argc, char **argv)
{
	BSHG_startTracing();
	return 0;
}

int bshg_trigger_spi(const struct shell *sh, size_t argc, char **argv)
{
	BSHG_spiTxRxShortcutFromHexstring(argv[1], strlen(argv[1]) - 1);
	return 0;
}

int bshg_trigger_json_n(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t start_cycle = k_cycle_get_32();

	int count = shell_strtol(argv[1], 10, NULL);
	json_msg reply = {0};
	json_msg msg = {0};
	strlcpy(msg, argv[2], sizeof(msg));

	LOG_INF("Info: Sending zbus message %d times: '%s'", count, msg);

	for (int i = 0; i < count; i++) {
		memset(reply, 0, sizeof(reply));
		trigger_message(&msg, 1000, &reply);
	}

	uint32_t elapsed_cycles = arch_k_cycle_get_32() - start_cycle;
	uint32_t time_us = k_cyc_to_us_near32(elapsed_cycles);

	LOG_INF("Info: Finished after %d µs: %s", time_us, reply);
	return 0;
}

int bshg_trigger_json(const struct shell *sh, size_t argc, char **argv)
{
	json_msg msg = {0};
	strlcpy(msg, argv[1], sizeof(msg));

	json_msg reply = {0};

	uint32_t start_cycles = arch_k_cycle_get_32();

	trigger_message(&msg, 3000, &reply);

	uint32_t elapsed_cycles = arch_k_cycle_get_32() - start_cycles;
	uint32_t time_us = k_cyc_to_us_near32(elapsed_cycles);
	LOG_INF("Info: Received reply after %d µs: %s", time_us, reply);
	return 0;
}

int bshg_trigger_forever(const struct shell *sh, size_t argc, char **argv)
{
	json_msg msg = {0};
	strlcpy(msg, argv[1], sizeof(msg));

	json_msg reply = {0};

	uint32_t start_cycles = arch_k_cycle_get_32();

	trigger_message(&msg, 3600000, &reply); // timeout 3600s

	uint32_t elapsed_cycles = arch_k_cycle_get_32() - start_cycles;
	uint32_t time_us = k_cyc_to_us_near32(elapsed_cycles);
	LOG_INF("Info: Received reply after %d µs: %s", time_us, reply);
	return 0;
}

int bshg_stress_info(const struct shell *sh, size_t argc, char **argv)
{
	int stress_level = get_stress_level();
	uint64_t base_time = get_stress_base_time_us();

	shell_print(sh, "CPU Stress %%:\t %d", stress_level);
	shell_print(sh, "CPU Base time:\t %lld", base_time);
	return 0;
}

int bshg_stress_cpu(const struct shell *sh, size_t argc, char **argv)
{
	int level = shell_strtol(argv[1], 10, NULL);
	uint64_t base_time = shell_strtol(argv[2], 10, NULL);
	set_stress_base_time(base_time);
	set_stress_level(level);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	bshg_trigger_sub, SHELL_CMD_ARG(led_on, NULL, "Trigger LED On", trigger_led_on, 1, 0),
	SHELL_CMD_ARG(led_off, NULL, "Trigger LED Off", trigger_led_off, 1, 0),
	SHELL_CMD_ARG(json, NULL, "Trigger a json message (and wait 3 seconds)", bshg_trigger_json,
		      2, 0),
	SHELL_CMD_ARG(json_forever, NULL, "Trigger a json message and wait forever",
		      bshg_trigger_forever, 2, 0),
	SHELL_CMD_ARG(json_n, NULL, "Trigger same json message n times as {key:value}",
		      bshg_trigger_json_n, 3, 0),
	SHELL_CMD_ARG(spi, NULL, "Trigger spi message", bshg_trigger_spi, 2, 0),
	SHELL_CMD_ARG(tracing_init, NULL, "Reset tracing tracebuffer", bshg_trigger_tracing_init, 1,
		      0),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

/******************************************************************************
 *
 * bshg stress commands
 *
 ******************************************************************************/

SHELL_STATIC_SUBCMD_SET_CREATE(bshg_stress_sub,
			       SHELL_CMD_ARG(cpu, NULL,
					     "Set stresslevel to x percent and base time in µs",
					     bshg_stress_cpu, 3, 0),
			       SHELL_CMD_ARG(ram, NULL, "", TODO, 2, 0),
			       SHELL_CMD_ARG(info, NULL, "Info about current stress",
					     bshg_stress_info, 1, 0),
			       SHELL_SUBCMD_SET_END /* Array terminated. */
);

/******************************************************************************
 *
 * bshg get commands
 *
 ******************************************************************************/
#ifdef CONFIG_TRACING_CTF
extern uint8_t ram_tracing[CONFIG_RAM_TRACING_BUFFER_SIZE];

int bshg_get_tracebuffer(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "--- tracebuffer start (%d bytes) ---", sizeof(ram_tracing));
	uint32_t index = 0;
	for (index = 0u; index < sizeof(ram_tracing); index++) {
		shell_fprintf(sh, SHELL_NORMAL, "%02x", ram_tracing[index]);
	}
	shell_fprintf(sh, SHELL_NORMAL, "\n");
	shell_print(sh, "--- tracebuffer end (%d bytes) ---", sizeof(ram_tracing));
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(bshg_get_sub,
			       SHELL_CMD_ARG(tracebuffer, NULL, "Get tracebuffer via UART",
					     bshg_get_tracebuffer, 1, 0),
			       SHELL_SUBCMD_SET_END /* Array terminated. */
);
#else
SHELL_STATIC_SUBCMD_SET_CREATE(bshg_get_sub,
			       SHELL_CMD_ARG(tracebuffer, NULL, "not supported", NULL, 0, 0),
			       SHELL_SUBCMD_SET_END /* Array terminated. */
);
#endif
/******************************************************************************
 *
 * shell commands
 *
 * syntax: SHELL_CMD_REGISTER(syntax, subcmd, help, handler)
 *
 ******************************************************************************/
SHELL_STATIC_SUBCMD_SET_CREATE(
	bshg_sub, SHELL_CMD_ARG(trigger, &bshg_trigger_sub, "trigger actions", TODO, 2, 0),
	SHELL_CMD_ARG(stress, &bshg_stress_sub, "interferring cpu or ram stress", TODO, 2, 0),
	SHELL_CMD_ARG(get, &bshg_get_sub, "get BSHG specific data", TODO, 2, 0),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(bshg, &bshg_sub, "bshg commands", NULL);
