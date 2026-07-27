/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define DISP_NODE DT_CHOSEN(zephyr_display)
#define WIDTH     DT_PROP(DISP_NODE, width)
#define HEIGHT    DT_PROP(DISP_NODE, height)

/* Simple 16-bit RGB565 buffer (one line only to save RAM) */
static uint16_t __aligned(32) line_buf[WIDTH];

int main(void)
{
	const struct device *display_dev;

	display_dev = DEVICE_DT_GET(DISP_NODE);
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return -1;
	}

	LOG_INF("Display device initialized: %s", display_dev->name);

	display_blanking_off(display_dev);

	struct display_buffer_descriptor buf_desc = {
		.width = WIDTH,
		.height = 1, /* one row at a time */
		.pitch = WIDTH,
		.buf_size = WIDTH * 2,
	};

	while (1) {

		/* Fill screen with vertical gradient */
		for (int y = 0; y < HEIGHT; y++) {
			uint16_t color =
				((y & 0x1F) << 11) | ((y & 0x3F) << 5); // red+green gradient
			for (int x = 0; x < WIDTH; x++) {
				line_buf[x] = color;
			}
			display_write(display_dev, 0, y, &buf_desc, line_buf);
		}

		LOG_INF("Color gradient1 written to display");
		k_sleep(K_SECONDS(2));

		/* Fill screen with vertical gradient */
		for (int y = 0; y < HEIGHT; y++) {
			uint16_t color = (y & 0x1F); // blue
			for (int x = 0; x < WIDTH; x++) {
				line_buf[x] = color;
			}
			display_write(display_dev, 0, y, &buf_desc, line_buf);
		}

		LOG_INF("Color gradient2 written to display");
		k_sleep(K_SECONDS(2));
	}
}
