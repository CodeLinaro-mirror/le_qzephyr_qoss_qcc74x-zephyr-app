/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/drivers/audio/audio_adc.h>
#include <zephyr/drivers/audio/audio_dac.h>

#define AUDIO_BUFFER_SIZE 16000

int main(void)
{
	const struct device *const audio_adc_dev = DEVICE_DT_GET(DT_NODELABEL(audio_adc));
	const struct device *const audio_dac_dev = DEVICE_DT_GET(DT_NODELABEL(audio_dac));
	static __aligned(32) uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
	int err;

	if (!device_is_ready(audio_adc_dev)) {
		printf("Audio ADC controller device %s not ready\n", audio_adc_dev->name);
		return 0;
	}

	struct audio_adc_cfg adc_cfg = {
		.buffer = audio_buffer,
		.buffer_size = AUDIO_BUFFER_SIZE,
		.sampling_rate = AUDIO_PCM_RATE_32K,
		.sample_width = AUDIO_PCM_WIDTH_16_BITS,
		.differential = false,
		.coupling_mode = AC,
		.input_positive = 4,
		.gain_db = 9
	};
	err = audio_adc_configure(audio_adc_dev, &adc_cfg);
	if (err < 0) {
		printf("Error in audio_adc_configure: %d\n", err);
	}

	if (!device_is_ready(audio_dac_dev)) {
		printf("Audio DAC controller device %s not ready\n", audio_dac_dev->name);
		return 0;
	}

	struct audio_dac_cfg dac_cfg = {
		.buffer = audio_buffer,
		.buffer_size = AUDIO_BUFFER_SIZE,
		.sampling_rate = AUDIO_PCM_RATE_32K,
		.sample_width = AUDIO_PCM_WIDTH_16_BITS
	};
	err = audio_dac_configure(audio_dac_dev, &dac_cfg);
	if (err < 0) {
		printf("Error in audio_dac_configure: %d\n", err);
	}

	while (true) {
		printf("Start loopback\n");
		err = audio_adc_start(audio_adc_dev);
		if (err < 0) {
			printf("Error in audio_adc_start: %d\n", err);
		}
		// Sleep some time to get audio buffer filled.
		k_sleep(K_MSEC(200));
		err = audio_dac_start(audio_dac_dev);
		if (err < 0) {
			printf("Error in audio_dac_start: %d\n", err);
		}
		k_sleep(K_SECONDS(5));

		printf("Stop loopback\n");
		err = audio_dac_stop(audio_dac_dev);
		if (err < 0) {
			printf("Error in audio_dac_stop: %d\n", err);
		}
		err = audio_adc_stop(audio_adc_dev);
		if (err < 0) {
			printf("Error in audio_adc_stop: %d\n", err);
		}
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
