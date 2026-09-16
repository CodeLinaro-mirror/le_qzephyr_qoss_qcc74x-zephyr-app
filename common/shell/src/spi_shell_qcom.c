/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Generic SPI shell commands for master and slave operation.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(spi_shell, LOG_LEVEL_INF);

/* ─── State ─────────────────────────────────────────────────────────────── */

#define SPI_MAX_BUF_SIZE CONFIG_QCOM_SPI_SHELL_BUFFER_SIZE
#define SPI_MAX_TRANSFER_BLOCKS 8
#define SPI_DEFAULT_FREQ 1000000
#define SPI_MAX_FREQUENCY 40000000U

static const struct device *spi_dev;

/* Runtime-configurable parameters */
static uint32_t cfg_frequency = SPI_DEFAULT_FREQ;
static uint8_t cfg_mode;        /* 0,1,2,3 */
static bool cfg_is_slave;       /* false=master, true=slave */
static uint8_t cfg_word_size = 8;
static bool cfg_lsb;           /* false=MSB, true=LSB */
static bool cfg_hold_cs;       /* keep CS/bus held until spi release */
/* Keep successive configurations at different addresses. The SPI driver
 * uses the config pointer as the identity of an immutable configuration. */
static struct spi_config config_slots[2];
static uint8_t config_slot;

/* Statistics */
static uint32_t stat_tx_bytes;
static uint32_t stat_rx_bytes;
static uint32_t stat_errors;

/* Slave async state */
static K_SEM_DEFINE(slave_sem, 0, 1);
/* Keep SPI DMA buffers in aligned, non-cacheable internal RAM.  Master and
 * slave roles are mutually exclusive, so both roles share these buffers. */
static uint8_t tx_buf[SPI_MAX_BUF_SIZE]
	__aligned(32) __attribute__((section(".nocache")));
static uint8_t rx_buf[SPI_MAX_BUF_SIZE]
	__aligned(32) __attribute__((section(".nocache")));
static size_t slave_tx_len;
static uint32_t slave_iterations;
static size_t slave_rx_len;
static volatile bool slave_done;
static volatile int slave_error;


/* ─── Helpers ───────────────────────────────────────────────────────────── */

static uint16_t build_spi_operation(void)
{
	uint16_t op = SPI_WORD_SET(cfg_word_size) | SPI_LINES_SINGLE;

	if (cfg_is_slave) {
		op |= SPI_OP_MODE_SLAVE;
	} else {
		op |= SPI_OP_MODE_MASTER;
	}

	switch (cfg_mode) {
	case 1:
		op |= SPI_MODE_CPHA;
		break;
	case 2:
		op |= SPI_MODE_CPOL;
		break;
	case 3:
		op |= SPI_MODE_CPOL | SPI_MODE_CPHA;
		break;
	default:
		break;
	}

	if (cfg_lsb) {
		op |= SPI_TRANSFER_LSB;
	}
	if (cfg_hold_cs) {
		op |= SPI_HOLD_ON_CS;
	}

	return op;
}

static const struct spi_config *build_spi_config(void)
{
	struct spi_config *config = &config_slots[config_slot];

	config_slot ^= 1U;
	*config = (struct spi_config){
		.frequency = cfg_frequency,
		.operation = build_spi_operation(),
		.slave = 0,
	};
	return config;
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t max_len, size_t *out_len)
{
	size_t hex_len = strlen(hex);

	if (hex_len % 2 != 0) {
		return -EINVAL;
	}

	size_t byte_len = hex_len / 2;

	if (byte_len > max_len) {
		return -ENOMEM;
	}

	for (size_t i = 0; i < byte_len; i++) {
		char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
		char *end;
		unsigned long val = strtoul(byte_str, &end, 16);

		if (*end != '\0') {
			return -EINVAL;
		}
		out[i] = (uint8_t)val;
	}
	*out_len = byte_len;
	return 0;
}

static void bytes_to_hex(const uint8_t *data, size_t len, char *out, size_t out_size)
{
	size_t pos = 0;

	for (size_t i = 0; i < len && pos + 2 < out_size; i++) {
		pos += snprintf(out + pos, out_size - pos, "%02X", data[i]);
	}
	out[pos] = '\0';
}

static void fill_pattern(uint8_t *buf, size_t len, const char *pattern)
{
	if (strcmp(pattern, "incr") == 0 || strcmp(pattern, "inc") == 0) {
		for (size_t i = 0; i < len; i++) {
			buf[i] = (uint8_t)(i & 0xFF);
		}
	} else if (strcmp(pattern, "random") == 0) {
		for (size_t i = 0; i < len; i++) {
			buf[i] = (uint8_t)(k_cycle_get_32() & 0xFF);
		}
	} else if (strncmp(pattern, "fixed:", 6) == 0) {
		uint8_t val = (uint8_t)strtoul(pattern + 6, NULL, 0);

		memset(buf, val, len);
	} else {
		/* Default: zero fill */
		memset(buf, 0, len);
	}
}

/* Arm the SPI peripheral in slave mode without blocking the shell thread. */
static void slave_work_fn(struct k_work *work);
static K_WORK_DEFINE(slave_work, slave_work_fn);

static void slave_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	const struct spi_config *config = build_spi_config();
	struct spi_buf tx_blocks[SPI_MAX_TRANSFER_BLOCKS];
	struct spi_buf rx_blocks[SPI_MAX_TRANSFER_BLOCKS];

	for (uint32_t block = 0; block < slave_iterations; block++) {
		tx_blocks[block].buf = tx_buf;
		tx_blocks[block].len = slave_tx_len;
		rx_blocks[block].buf = rx_buf;
		rx_blocks[block].len = slave_tx_len;
	}

	const struct spi_buf_set tx_set = {
		.buffers = tx_blocks, .count = slave_iterations
	};
	const struct spi_buf_set rx_set = {
		.buffers = rx_blocks, .count = slave_iterations
	};

	int ret = spi_transceive(spi_dev, config, &tx_set, &rx_set);

	if (ret >= 0) {
		slave_rx_len = slave_tx_len * slave_iterations;
		stat_tx_bytes += (uint32_t)(slave_tx_len * slave_iterations);
		stat_rx_bytes += (uint32_t)(slave_tx_len * slave_iterations);
	} else {
		slave_rx_len = 0;
		stat_errors++;
		LOG_ERR("Slave transceive failed: %d", ret);
	}

	slave_done = true;
	slave_error = (ret < 0) ? ret : 0;
	k_sem_give(&slave_sem);
}


/* ─── Shell Commands ────────────────────────────────────────────────────── */

static int ensure_spi_ready(const struct shell *sh)
{
	if (spi_dev == NULL) {
		spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi0));
	}

	if (!device_is_ready(spi_dev)) {
		shell_error(sh, "ERROR: SPI device not ready");
		return -ENODEV;
	}

	return 0;
}

static int cmd_spi_config(const struct shell *sh, size_t argc, char **argv)
{
	if (ensure_spi_ready(sh) != 0) {
		return -ENODEV;
	}

	/* Parse optional args */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
			uint32_t frequency = (uint32_t)strtoul(argv[++i], NULL, 10);
			if (frequency == 0U || frequency > SPI_MAX_FREQUENCY) {
				shell_error(sh, "ERROR: invalid frequency %u (1-%u Hz)",
					    frequency, SPI_MAX_FREQUENCY);
				return -EINVAL;
			}
			cfg_frequency = frequency;
		} else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
			cfg_mode = (uint8_t)strtoul(argv[++i], NULL, 10);
			if (cfg_mode > 3) {
				shell_error(sh, "ERROR: invalid mode %d (0-3)", cfg_mode);
				return -EINVAL;
			}
		} else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
			i++;
			if (strcmp(argv[i], "master") == 0) {
				cfg_is_slave = false;
			} else if (strcmp(argv[i], "slave") == 0) {
				cfg_is_slave = true;
			} else {
				shell_error(sh, "ERROR: invalid role '%s'", argv[i]);
				return -EINVAL;
			}
		} else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
			cfg_word_size = (uint8_t)strtoul(argv[++i], NULL, 10);
			if (cfg_word_size != 8 && cfg_word_size != 16 &&
			    cfg_word_size != 24 && cfg_word_size != 32) {
				shell_error(sh, "ERROR: invalid word size %d", cfg_word_size);
				return -EINVAL;
			}
		} else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			i++;
			if (strcmp(argv[i], "lsb") == 0) {
				cfg_lsb = true;
			} else if (strcmp(argv[i], "msb") == 0) {
				cfg_lsb = false;
			} else {
				shell_error(sh, "ERROR: invalid bit order '%s' (msb|lsb)", argv[i]);
				return -EINVAL;
			}
		}
	}

	shell_print(sh, "OK: SPI configuration staged [freq=%u mode=%u role=%s word=%u order=%s]",
		    cfg_frequency, cfg_mode,
		    cfg_is_slave ? "slave" : "master",
		    cfg_word_size,
		    cfg_lsb ? "lsb" : "msb");
	return 0;
}

static int cmd_spi_transceive(const struct shell *sh, size_t argc, char **argv)
{
	if (ensure_spi_ready(sh) != 0) {
		return -ENODEV;
	}
	if (cfg_is_slave) {
		shell_error(sh, "ERROR: transceive command is for master mode only");
		return -EINVAL;
	}

	size_t len = 0;
	uint32_t repetitions = 1;
	bool use_pattern = false;
	bool hold_cs_requested = false;
	const char *pattern = "incr";
	size_t pattern_len = 32;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0) {
			hold_cs_requested = true;
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			use_pattern = true;
			pattern = argv[++i];
		} else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
			pattern_len = (size_t)strtoul(argv[++i], NULL, 10);
		} else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			repetitions = (uint32_t)strtoul(argv[++i], NULL, 10);
		} else if (!use_pattern && len == 0) {
			int ret = hex_to_bytes(argv[i], tx_buf, SPI_MAX_BUF_SIZE, &len);

			if (ret != 0) {
				shell_error(sh, "ERROR: invalid hex data");
				return ret;
			}
		}
	}

	if (use_pattern) {
		if (pattern_len > SPI_MAX_BUF_SIZE) {
			pattern_len = SPI_MAX_BUF_SIZE;
		}
		fill_pattern(tx_buf, pattern_len, pattern);
		len = pattern_len;
	}

	if (len == 0) {
		shell_error(sh, "ERROR: no data specified");
		return -EINVAL;
	}
	if (repetitions == 0 || repetitions > SPI_MAX_TRANSFER_BLOCKS) {
		shell_error(sh, "ERROR: repetitions must be 1-%u", SPI_MAX_TRANSFER_BLOCKS);
		return -EINVAL;
	}

	/* HOLD_ON_CS requires the interrupt transfer path on this controller. */
	if (hold_cs_requested && !IS_ENABLED(CONFIG_SPI_QCC74X_INTERRUPT)) {
		shell_error(sh, "ERROR: -c is supported in SPI interrupt mode only");
		return -ENOTSUP;
	}
	if (hold_cs_requested) {
		cfg_hold_cs = true;
	}

	memset(rx_buf, 0, len);

	const struct spi_config *config = build_spi_config();
	struct spi_buf tx_blocks[SPI_MAX_TRANSFER_BLOCKS];
	struct spi_buf rx_blocks[SPI_MAX_TRANSFER_BLOCKS];
	for (uint32_t block = 0; block < repetitions; block++) {
		tx_blocks[block] = (struct spi_buf){ .buf = tx_buf, .len = len };
		rx_blocks[block] = (struct spi_buf){ .buf = rx_buf, .len = len };
	}
	const struct spi_buf_set tx_set = { .buffers = tx_blocks, .count = repetitions };
	const struct spi_buf_set rx_set = { .buffers = rx_blocks, .count = repetitions };

	int ret = spi_transceive(spi_dev, config, &tx_set, &rx_set);

	if (ret != 0) {
		stat_errors++;
		shell_error(sh, "ERROR: transfer failed (%d)", ret);
		return ret;
	}

	uint32_t total_len = (uint32_t)(len * repetitions);
	stat_tx_bytes += total_len;
	stat_rx_bytes += total_len;

	char tx_hex[256], rx_hex[256];

	if (len <= 128) {
		bytes_to_hex(tx_buf, len, tx_hex, sizeof(tx_hex));
		bytes_to_hex(rx_buf, len, rx_hex, sizeof(rx_hex));
	} else {
		bytes_to_hex(tx_buf, 64, tx_hex, sizeof(tx_hex));
		bytes_to_hex(rx_buf, 64, rx_hex, sizeof(rx_hex));
		strcat(tx_hex, "...");
		strcat(rx_hex, "...");
	}

	shell_print(sh, "OK: transceived %u bytes in %u buffer(s)",
		    (unsigned int)total_len, repetitions);
	shell_print(sh, "TX: %s", tx_hex);
	shell_print(sh, "RX: %s", rx_hex);
	return 0;
}

static int cmd_spi_slave_prepare(const struct shell *sh, size_t argc, char **argv)
{
	if (ensure_spi_ready(sh) != 0) {
		return -ENODEV;
	}
	if (!cfg_is_slave) {
		shell_error(sh, "ERROR: slave_prepare is for slave mode only");
		return -EINVAL;
	}

	size_t len = 0;
	bool use_pattern = false;
	const char *pattern = "incr";
	size_t pattern_len = 32;
	uint32_t repetitions = 1;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			use_pattern = true;
			pattern = argv[++i];
		} else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
			pattern_len = (size_t)strtoul(argv[++i], NULL, 10);
		} else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			repetitions = (uint32_t)strtoul(argv[++i], NULL, 10);
		} else if (!use_pattern && len == 0) {
			int ret = hex_to_bytes(argv[i], tx_buf, SPI_MAX_BUF_SIZE, &len);

			if (ret != 0) {
				shell_error(sh, "ERROR: invalid hex data");
				return ret;
			}
		}
	}

	if (use_pattern) {
		if (pattern_len > SPI_MAX_BUF_SIZE) {
			pattern_len = SPI_MAX_BUF_SIZE;
		}
		fill_pattern(tx_buf, pattern_len, pattern);
		len = pattern_len;
	}

	if (len == 0) {
		shell_error(sh, "ERROR: no data specified");
		return -EINVAL;
	}
	if (repetitions == 0 || repetitions > SPI_MAX_TRANSFER_BLOCKS) {
		shell_error(sh, "ERROR: repetitions must be 1-%u", SPI_MAX_TRANSFER_BLOCKS);
		return -EINVAL;
	}

	slave_tx_len = len;
	slave_iterations = repetitions;
	slave_rx_len = 0;
	slave_done = false;
	slave_error = 0;
	k_sem_reset(&slave_sem);

	/* Submit work to background thread - blocks in spi_transceive until master drives clock */
	k_work_submit(&slave_work);

	shell_print(sh, "OK: slave prepared %u bytes x %u, waiting for master",
		    (unsigned int)len, repetitions);
	return 0;
}

static int cmd_spi_slave_read(const struct shell *sh, size_t argc, char **argv)
{
	if (ensure_spi_ready(sh) != 0) {
		return -ENODEV;
	}
	if (!cfg_is_slave) {
		shell_error(sh, "ERROR: slave_read is for slave mode only");
		return -EINVAL;
	}

	uint32_t timeout_ms = 5000;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
			timeout_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
		}
	}

	int ret = k_sem_take(&slave_sem, K_MSEC(timeout_ms));

	if (ret != 0) {
		shell_error(sh, "ERROR: timeout waiting for data");
		return -ETIMEDOUT;
	}

	if (!slave_done || slave_rx_len == 0) {
		if (slave_error == -ETIMEDOUT) {
			shell_error(sh, "ERROR: slave hardware timeout");
			return -ETIMEDOUT;
		}
		shell_error(sh, "ERROR: no data received");
		return -EIO;
	}

	char hex_str[256];

	if (slave_rx_len <= 128) {
	bytes_to_hex(rx_buf, slave_rx_len, hex_str, sizeof(hex_str));
	} else {
		bytes_to_hex(rx_buf, 64, hex_str, sizeof(hex_str));
		strcat(hex_str, "...");
	}

	shell_print(sh, "OK: slave received %u bytes", (unsigned int)slave_rx_len);
	shell_print(sh, "DATA: %s", hex_str);
	return 0;
}


/* Zephyr standard API: release a bus/CS held by SPI_HOLD_ON_CS. */
static int cmd_spi_release(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (ensure_spi_ready(sh) != 0) {
		return -ENODEV;
	}

	const struct spi_config *config = build_spi_config();
	int ret = spi_release(spi_dev, config);

	if (ret != 0) {
		stat_errors++;
		shell_error(sh, "ERROR: SPI release failed (%d)", ret);
		return ret;
	}

	cfg_hold_cs = false;
	shell_print(sh, "OK: SPI released");
	return 0;
}

static int cmd_spi_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "qcom spi Status:");
	shell_print(sh, "  Device: spi0");
	shell_print(sh, "  State: %s", device_is_ready(spi_dev) ? "ready" : "not_ready");
	shell_print(sh, "  Role: %s", cfg_is_slave ? "slave" : "master");
	shell_print(sh, "  Frequency: %u Hz", cfg_frequency);
	shell_print(sh, "  Mode: %u (CPOL=%u, CPHA=%u)", cfg_mode,
		    (cfg_mode >> 1) & 1, cfg_mode & 1);
	shell_print(sh, "  Word size: %u bits", cfg_word_size);
	shell_print(sh, "  Bit order: %s first", cfg_lsb ? "LSB" : "MSB");
	shell_print(sh, "  TX count: %u", stat_tx_bytes);
	shell_print(sh, "  RX count: %u", stat_rx_bytes);
	shell_print(sh, "  Error count: %u", stat_errors);
	return 0;
}

/*
 * Shell API layers:
 *   - config/transceive/release: Zephyr SPI API exercises.
 *   - status: inspect the runtime SPI configuration and transfer statistics.
 */

SHELL_STATIC_SUBCMD_SET_CREATE(spi_cmds,
	SHELL_CMD_ARG(config, NULL,
		"Stage Zephyr SPI configuration parameters\n"
		"Usage: qcom spi config [-f freq] [-m mode] [-r master|slave] [-w 8|16|24|32] [-o msb|lsb]",
		cmd_spi_config, 1, 12),
	SHELL_CMD_ARG(transceive, NULL,
		"Full-duplex transfer (Master)\n"
		"Usage: qcom spi transceive <hex_data>\n"
		"       qcom spi transceive -p <pattern> -l <length> [-n count] [-c]\n"
		"       -n repeats the same buffer in one SPI transaction\n"
		"       -c holds CS after this master transfer",
		cmd_spi_transceive, 2, 6),
	SHELL_CMD_ARG(slave_prepare, NULL,
		"Prepare slave TX data and wait for master\n"
		"Usage: qcom spi slave_prepare <hex_data> [-n count]\n"
		"       qcom spi slave_prepare -p <pattern> -l <length> [-n count]",
		cmd_spi_slave_prepare, 2, 6),
	SHELL_CMD_ARG(slave_read, NULL,
		"Read slave RX data (blocks until received or timeout)\n"
		"Usage: qcom spi slave_read [-t timeout_ms]",
		cmd_spi_slave_read, 1, 2),
	SHELL_CMD_ARG(release, NULL,
		"Release a Zephyr SPI bus/CS held by -c\n"
		"Usage: qcom spi release",
		cmd_spi_release, 1, 0),
	SHELL_CMD_ARG(status, NULL,
		"Show SPI status and transfer statistics",
		cmd_spi_status, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_SUBCMD_ADD((qcom), spi, &spi_cmds,
	"QCC74x SPI API commands", NULL, 1, 0);
