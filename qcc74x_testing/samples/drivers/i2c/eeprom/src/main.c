/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(eeprom_test, LOG_LEVEL_INF);

#define EEPROM_NODE DT_CHOSEN(zephyr_eeprom)

#define EEPROM_PAGE_SIZE DT_PROP(EEPROM_NODE, pagesize)
#define EEPROM_MAX_RETRIES 8
static uint8_t __attribute((aligned(32))) write_buf[EEPROM_PAGE_SIZE];
static uint8_t __attribute((aligned(32))) read_buf[EEPROM_PAGE_SIZE];

static double calculate_throughput_kbps(uint64_t total_bytes, uint32_t op_start_ms,
					uint32_t op_end_ms, uint32_t excluded_ms)
{
	uint32_t active_ms = op_end_ms - op_start_ms - excluded_ms;
	double elapsed_seconds = active_ms / 1000.0;

	if (elapsed_seconds <= 0.0) {
		elapsed_seconds = 0.001;
	}

	return (total_bytes / 1024.0) / elapsed_seconds;
}

static int write_eeprom_pattern(const struct device *eeprom, size_t eeprom_size,
				uint8_t pattern_seed, size_t *bytes_written,
				double *throughput_kbps)
{
	off_t eeprom_offset = 0;
	int status;
	uint32_t pattern_gen_ms = 0;
	uint32_t op_start_ms = k_uptime_get_32();
	uint64_t total_bytes_written = 0;
	uint8_t write_error_count = 0;

	while (eeprom_offset < eeprom_size) {
		size_t page_chunk_size = MIN(EEPROM_PAGE_SIZE, eeprom_size - eeprom_offset);

		/* Fill write buffer with pattern based on offset */
		/* Exclude pattern generation time from benchmark */
		uint32_t pattern_gen_start_ms = k_uptime_get_32();
		for (size_t i = 0; i < page_chunk_size; i++) {
			write_buf[i] = (uint8_t)((eeprom_offset + i + pattern_seed) % 256);
		}
		pattern_gen_ms += (k_uptime_get_32() - pattern_gen_start_ms);

		uint32_t write_start_ms = k_uptime_get_32();
		status = eeprom_write(eeprom, eeprom_offset, write_buf, page_chunk_size);
		if (status != 0) {
			double write_elapsed_seconds =
				(k_uptime_get_32() - write_start_ms) / 1000.0;
			LOG_ERR("Write failed at offset %zu: elapsed %.6f s, error %d",
				(size_t)eeprom_offset, write_elapsed_seconds, status);
			write_error_count++;
			if (write_error_count >= EEPROM_MAX_RETRIES) {
				return status;
			}
		}

		eeprom_offset += page_chunk_size;
		total_bytes_written += page_chunk_size;
	}

	*throughput_kbps = calculate_throughput_kbps(total_bytes_written, op_start_ms,
						     k_uptime_get_32(), pattern_gen_ms);
	*bytes_written = (size_t)eeprom_offset;

	return 0;
}

static int read_verify_eeprom_pattern(const struct device *eeprom, size_t eeprom_size,
				      uint8_t pattern_seed, size_t *bytes_read,
				      double *throughput_kbps)
{
	off_t eeprom_offset = 0;
	int status;
	uint64_t total_bytes_read = 0;
	uint32_t pattern_verify_ms = 0;
	uint32_t op_start_ms = k_uptime_get_32();
	uint8_t read_error_count = 0;

	while (eeprom_offset < eeprom_size) {
		size_t page_chunk_size = MIN(EEPROM_PAGE_SIZE, eeprom_size - eeprom_offset);

		status = eeprom_read(eeprom, eeprom_offset, read_buf, page_chunk_size);
		if (status != 0) {
			LOG_ERR("Read failed at offset %zu: error %d", (size_t)eeprom_offset,
				status);
			read_error_count++;
			if (read_error_count >= EEPROM_MAX_RETRIES) {
				return status;
			}
		}

		/* Verify the read data matches the written pattern */
		/* Exclude pattern generation time from benchmark */
		uint32_t verify_start_ms = k_uptime_get_32();
		for (size_t i = 0; i < page_chunk_size; i++) {
			uint8_t expected_byte = (uint8_t)((eeprom_offset + i + pattern_seed) % 256);
			if (read_buf[i] != expected_byte) {
				LOG_ERR("Data mismatch at address %zu: expected %u, got %u",
					(size_t)(eeprom_offset + i), expected_byte, read_buf[i]);
				return -1;
			}
		}

		eeprom_offset += page_chunk_size;
		pattern_verify_ms += (k_uptime_get_32() - verify_start_ms);
		total_bytes_read += page_chunk_size;
	}

	*throughput_kbps = calculate_throughput_kbps(total_bytes_read, op_start_ms,
						     k_uptime_get_32(), pattern_verify_ms);
	*bytes_read = (size_t)eeprom_offset;

	return 0;
}

int main(void)
{
	int status;
	uint8_t pattern_seed = (uint8_t)sys_rand32_get();

	/*
	 * Get the EEPROM device. This assumes the DTS has a chosen node 'zephyr,eeprom'.
	 * Adjust if your board uses a different label, e.g., DEVICE_DT_GET(DT_NODELABEL(eeprom0)).
	 */
	const struct device *const eeprom = DEVICE_DT_GET(EEPROM_NODE);

	if (!device_is_ready(eeprom)) {
		LOG_ERR("EEPROM device is not ready");
		return 0;
	}

	/* Read EEPROM size */
	size_t eeprom_size = eeprom_get_size(eeprom);
	LOG_INF("EEPROM total size: %zu bytes", eeprom_size);
	LOG_INF("EEPROM page size: %d bytes", DT_PROP(EEPROM_NODE, pagesize));
	LOG_INF("Pseudo-random pattern seed: %u", pattern_seed);

	/* Write data to EEPROM in page-size chunks with a repeating pattern (0-255) */
	LOG_INF("Starting EEPROM write operation");
	double throughput_kbps = 0;
	size_t total_bytes_written = 0;
	status = write_eeprom_pattern(eeprom, eeprom_size, pattern_seed, &total_bytes_written,
				      &throughput_kbps);
	if (status != 0) {
		return 0;
	}

	LOG_INF("EEPROM write complete: %zu bytes written", total_bytes_written);
	LOG_INF("Write throughput: %.2f KB/s", throughput_kbps);

	/* Read back and verify the data in page-size chunks */
	LOG_INF("Starting EEPROM read and verify operation");
	size_t total_bytes_read = 0;
	status = read_verify_eeprom_pattern(eeprom, eeprom_size, pattern_seed, &total_bytes_read,
					    &throughput_kbps);
	if (status != 0) {
		return 0;
	}

	LOG_INF("EEPROM read and verify complete: %zu bytes checked, all data matches",
		total_bytes_read);
	LOG_INF("Read throughput: %.2f KB/s", throughput_kbps);

	LOG_INF("EEPROM test completed successfully");

	return 0;
}
