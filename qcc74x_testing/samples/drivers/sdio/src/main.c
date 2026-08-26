/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/cache.h>
#include <zephyr/drivers/sdio_device.h>

#define SDIO_TEST_SIZE    (2 * 1024)
#define SDIO_MAX_PORT_NUM 16

volatile uint32_t dnld_cnt = 0, upld_cnt = 0;

/* T-L-V struct */
typedef struct {
	uint16_t tag;
	uint16_t length;
	uint8_t value[0];
} sdio_tlv_t;

static const struct device *sdio = DEVICE_DT_GET(DT_NODELABEL(sdio));

void sdio_dnld_irq_callback(const struct device *dev, sdio_transfer_desc_t *trans_desc)
{
	/* dnld done */
	dnld_cnt++;
	/* invalid cache */
	sys_cache_data_invd_range(trans_desc->buff, trans_desc->data_len);

	sdio_tlv_t *tlv = (sdio_tlv_t *)trans_desc->buff;

	/* update dnld data_len */
	trans_desc->data_len = tlv->length + sizeof(sdio_tlv_t);

	printk("dnld done, tag 0x%04X, len %d, upld it!\n", tlv->tag,
	       (tlv->length + sizeof(sdio_tlv_t)));

	/* push to upld queue*/
	sdio_upld_port_push(dev, trans_desc);
}

void sdio_upld_irq_callback(const struct device *dev, sdio_transfer_desc_t *trans_desc)
{
	/* upld done */
	upld_cnt++;

	/* max upld buff len */
	trans_desc->buff_len = SDIO_TEST_SIZE;

	printk("upld done, push buff to dnld queue\n");

	/* push to dnld queue */
	sdio_dnld_port_push(dev, trans_desc);
}

void sdio_reset_irq_callback(const struct device *dev)
{
	printk("\r\n***** sdio reset! *****\n");

	/* clean cnt */
	dnld_cnt = 0;
	upld_cnt = 0;

	/* free test buff */
	{
		sdio_transfer_desc_t trans_desc;
		int free_n = 0;
		printk("free dnld queue buff\n");
		while (sdio_dnld_port_pop(dev, &trans_desc) == 0) {
			free(trans_desc.buff);
			free_n++;
		}

		printk("free upld queue buff\n");
		while (sdio_upld_port_pop(dev, &trans_desc) == 0) {
			free(trans_desc.buff);
			free_n++;
		}

		printk("buff free num:%d\n", free_n);
	}

	/* sdio queue ctrl reset */
	sdio_re_initialize(dev);

	/* link all dnld buff */
	printk("malloc buff and push to dnld_queue\n");
	for (uint32_t i = 0; i < SDIO_MAX_PORT_NUM; i++) {
		void *buff = malloc(SDIO_TEST_SIZE);
		if (buff == NULL) {
			printk("malloc NULL\n");
			return;
		}

		sdio_transfer_desc_t dnld_desc = {
			.buff = buff,
			.buff_len = SDIO_MAX_PORT_NUM,
			.user_arg = NULL,
		};
		sdio_dnld_port_push(dev, &dnld_desc);
	}
}

int main()
{
	uint32_t dnld_cnt_last = 0, upld_cnt_last = 0;

	while (!device_is_ready(sdio)) {
		k_msleep(10);
	}

	sdio_set_download_callback(sdio, sdio_dnld_irq_callback);
	sdio_set_upload_callback(sdio, sdio_upld_irq_callback);
	sdio_set_reset_callback(sdio, sdio_reset_irq_callback);

	for (uint32_t i = 0; i < SDIO_MAX_PORT_NUM; i++) {
		void *buff = malloc(SDIO_TEST_SIZE);
		if (buff == NULL) {
			printk("malloc NULL\r\n");
			return -1;
		}

		sdio_transfer_desc_t dnld_desc = {
			.buff = buff,
			.buff_len = SDIO_TEST_SIZE,
			.user_arg = NULL,
		};
		sdio_dnld_port_push(sdio, &dnld_desc);
	}

	/* wait host ready */
	printk("sdio2 wait host ready...\r\n");
	while (sdio_feature_control(sdio, SDIO_CMD_GET_FUNC_HOST_READY, 0) == 0) {
		k_msleep(1);
	}

	printk("sdio2 host ready! \r\n");
	/* bus width */
	printk("sdio2 bus width: %d\r\n", sdio_feature_control(sdio, SDIO_CMD_GET_BUS_WIDTH, 0));
	/* get block size */
	printk("sdio2 block size: %d\r\n",
	       sdio_feature_control(sdio, SDIO_CMD_GET_FUNC_BLK_SIZE, 0));
	/* get max size */
	printk("sdio dnld max size %d\r\n",
	       sdio_feature_control(sdio, SDIO_CMD_GET_DNLD_MAX_SIZE, 0));
	printk("sdio upld max size %d\r\n",
	       sdio_feature_control(sdio, SDIO_CMD_GET_UPLD_MAX_SIZE, 0));

	/* print info */
	while (1) {
		if (dnld_cnt_last != dnld_cnt || upld_cnt_last != upld_cnt) {
			dnld_cnt_last = dnld_cnt;
			upld_cnt_last = upld_cnt;
			printk("sdio2 test, dnld_cnt: %d, upld_cnt: %d\r\n", dnld_cnt_last,
			       upld_cnt_last);
		}
		k_msleep(250);
	}
	return 0;
}