/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video/jpeg_qcc74x.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <string.h>

#include "test_64x64.h"
#include "test_352x288.h"

#define WIDTH   64
#define HEIGHT  64
#define WIDTH1  352
#define HEIGHT1 288

#define JPEG_QUALITY 50

const struct device *jpeg_dev;

__aligned(32) static uint8_t test_buf[WIDTH1 * HEIGHT1 * 2 + 8];
__aligned(32) static uint8_t jpeg_out[32 * 1024];

static struct video_buffer in_buf;
static struct video_buffer out_buf;

static void dump_hex(uint8_t *data, uint32_t len)
{
	uint32_t i = 0;
	for (i = 0; i < len; i++) {
		if (i % 16 == 0 && i) {
			printk("\r\n");
		}
		printk("%02x ", data[i]);
	}
	printk("\r\n");
}

int main(void)
{
	struct video_format fmt;
	int ret = 0;

	printk("\r\n");

	jpeg_dev = DEVICE_DT_GET(DT_NODELABEL(jpeg));
	if (!jpeg_dev) {
		printk("JPEG device not found\n");
		return 0;
	}

	/* Set JPEG quality */
	struct video_control quality_ctrl = {
		.id = VIDEO_CID_JPEG_COMPRESSION_QUALITY,
		.val = JPEG_QUALITY,
	};
	ret = video_set_ctrl(jpeg_dev, &quality_ctrl);
	if (ret) {
		printk("Failed to set JPEG quality: %d\n", ret);
	}

	/* --- First image --- */

	/* Set input format */
	memset(&fmt, 0, sizeof(fmt));
	fmt.pixelformat = VIDEO_PIX_FMT_YUYV;
	fmt.width = WIDTH;
	fmt.height = HEIGHT;
	fmt.pitch = WIDTH * 2;

	ret = video_set_format(jpeg_dev, VIDEO_EP_IN, &fmt);
	if (ret) {
		printk("Failed to set IN format: %d\n", ret);
		return 0;
	}

	/* Set output format */
	memset(&fmt, 0, sizeof(fmt));
	fmt.pixelformat = VIDEO_PIX_FMT_JPEG;
	fmt.width = WIDTH;
	fmt.height = HEIGHT;

	ret = video_set_format(jpeg_dev, VIDEO_EP_OUT, &fmt);
	if (ret) {
		printk("Failed to set OUT format: %d\n", ret);
		return 0;
	}

	memset(&out_buf, 0, sizeof(out_buf));
	in_buf.buffer = test_64x64;
	in_buf.size = sizeof(test_64x64);
	out_buf.buffer = jpeg_out;
	out_buf.size = sizeof(jpeg_out);

	/* Enqueue pair of buffers */
	video_enqueue(jpeg_dev, VIDEO_EP_OUT, &out_buf);
	video_enqueue(jpeg_dev, VIDEO_EP_IN, &in_buf);

	/* Start encoder */
	video_stream_start(jpeg_dev);
	printk("Encoding %dx%d ...\n", WIDTH, HEIGHT);

	struct video_buffer *done_buf = NULL;
	ret = video_dequeue(jpeg_dev, VIDEO_EP_OUT, &done_buf, K_FOREVER);
	if (ret) {
		printk("Dequeue error: %d\n", ret);
		return 0;
	}
	if (ret == 0 && done_buf != NULL) {
		printk("JPEG encoded OK — size = %u bytes, %d ms\n", out_buf.bytesused,
		       out_buf.timestamp - in_buf.timestamp);
		dump_hex(jpeg_out, 16);
		dump_hex(jpeg_out + 800, 16);
		printk("\n\n");
	}
	/* Dequeue input buffer for flush fifo */
	video_dequeue(jpeg_dev, VIDEO_EP_IN, &done_buf, K_FOREVER);
	video_stream_stop(jpeg_dev);

	/* --- Second image --- */

	/* Set input format */
	memset(&fmt, 0, sizeof(fmt));
	fmt.pixelformat = VIDEO_PIX_FMT_YUYV;
	fmt.width = WIDTH1;
	fmt.height = HEIGHT1;
	fmt.pitch = WIDTH1 * 2;

	ret = video_set_format(jpeg_dev, VIDEO_EP_IN, &fmt);
	if (ret) {
		printk("Failed to set IN format: %d\n", ret);
		return 0;
	}

	/* Set output format */
	memset(&fmt, 0, sizeof(fmt));
	fmt.pixelformat = VIDEO_PIX_FMT_JPEG;
	fmt.width = WIDTH1;
	fmt.height = HEIGHT1;

	ret = video_set_format(jpeg_dev, VIDEO_EP_OUT, &fmt);
	if (ret) {
		printk("Failed to set OUT format: %d\n", ret);
		return 0;
	}

	memset(&out_buf, 0, sizeof(out_buf));
	in_buf.buffer = test_352x288;
	in_buf.size = sizeof(test_352x288);
	out_buf.buffer = jpeg_out;
	out_buf.size = sizeof(jpeg_out);
	video_enqueue(jpeg_dev, VIDEO_EP_OUT, &out_buf);
	video_enqueue(jpeg_dev, VIDEO_EP_IN, &in_buf);

	/* Start encoder */
	video_stream_start(jpeg_dev);
	printk("Encoding %dx%d ...\n", WIDTH1, HEIGHT1);

	struct video_buffer *done_buf1 = NULL;
	ret = video_dequeue(jpeg_dev, VIDEO_EP_OUT, &done_buf1, K_FOREVER);
	if (ret) {
		printk("Dequeue error: %d\n", ret);
		return 0;
	}

	if (ret == 0 && done_buf1 != NULL) {
		printk("JPEG encoded OK — size = %u bytes, %d ms\n", out_buf.bytesused,
		       out_buf.timestamp - in_buf.timestamp);
		dump_hex(jpeg_out, 16);
		dump_hex(jpeg_out + 800, 16);
		printk("\n\n");
	}
	/* Dequeue input buffer for flush fifo */
	video_dequeue(jpeg_dev, VIDEO_EP_IN, &done_buf1, K_FOREVER);
	video_stream_stop(jpeg_dev);

	/* --- Third image --- */

	/* Set input format */
	memset(&fmt, 0, sizeof(fmt));
	fmt.pixelformat = VIDEO_PIX_FMT_YUYV;
	fmt.width = WIDTH1;
	fmt.height = HEIGHT1;
	fmt.pitch = WIDTH1 * 2;

	ret = video_set_format(jpeg_dev, VIDEO_EP_IN, &fmt);
	if (ret) {
		printk("Failed to set IN format: %d\n", ret);
		return 0;
	}

	/* Set output format */
	memset(&fmt, 0, sizeof(fmt));
	fmt.pixelformat = VIDEO_PIX_FMT_JPEG;
	fmt.width = WIDTH1;
	fmt.height = HEIGHT1;

	ret = video_set_format(jpeg_dev, VIDEO_EP_OUT, &fmt);
	if (ret) {
		printk("Failed to set OUT format: %d\n", ret);
		return 0;
	}

	memset(&out_buf, 0, sizeof(out_buf));
	in_buf.buffer = test_buf;
	in_buf.size = sizeof(test_buf);

	out_buf.buffer = jpeg_out;
	out_buf.size = sizeof(jpeg_out);
	video_enqueue(jpeg_dev, VIDEO_EP_OUT, &out_buf);
	video_enqueue(jpeg_dev, VIDEO_EP_IN, &in_buf);

	/* Start encoder */
	video_stream_start(jpeg_dev);
	printk("Encoding %dx%d ...\n", WIDTH1, HEIGHT1);

	/* Start again */
	ret = video_stream_start(jpeg_dev);
	printk("Encoding start again %d (%s) OK\n", ret, strerror(-ret));

	/* Set video format while stream is enabled*/
	ret = video_set_format(jpeg_dev, VIDEO_EP_OUT, &fmt);
	printk("Failed to set OUT format when stream enabled: %d (%s) OK\n", ret, strerror(-ret));

	struct video_buffer *done_buf2 = NULL;
	ret = video_dequeue(jpeg_dev, VIDEO_EP_OUT, &done_buf2, K_FOREVER);
	if (ret) {
		printk("Dequeue error: %d\n", ret);
		return 0;
	}

	if (ret == 0 && done_buf2 != NULL) {
		printk("JPEG encoded OK — size = %u bytes, %d ms\n", out_buf.bytesused,
		       out_buf.timestamp - in_buf.timestamp);
		dump_hex(jpeg_out, 16);
		dump_hex(jpeg_out + 800, 16);
		printk("\n\n");
	}
	/* Dequeue input buffer for flush fifo */
	video_dequeue(jpeg_dev, VIDEO_EP_IN, &done_buf2, K_FOREVER);
	video_stream_stop(jpeg_dev);

	/* Check if output fifos is empty */
	ret = video_dequeue(jpeg_dev, VIDEO_EP_OUT, &done_buf2, K_MSEC(50));
	if (ret) {
		printk("Dequeue empty VIDEO_EP_OUT, ret %d (%s) OK\n", ret, strerror(-ret));
	}
	ret = video_dequeue(jpeg_dev, VIDEO_EP_IN, &done_buf2, K_MSEC(50));
	if (ret) {
		printk("Dequeue empty VIDEO_EP_IN, ret %d (%s) OK\n", ret, strerror(-ret));
	}

	/* Start with empty input */
	ret = video_stream_start(jpeg_dev);
	printk("Encoding with empty in/out fifos %d (%s) OK\n", ret, strerror(-ret));
	ret = video_stream_stop(jpeg_dev);
	printk("Stream stop %d (%s) OK\n", ret, strerror(-ret));

	/* Add x pairs of buffers and encode all */
	printk("\r\n\r\n");

	uint8_t queue_size = 9;
	/* Set input format */
	memset(&fmt, 0, sizeof(fmt));
	fmt.pixelformat = VIDEO_PIX_FMT_YUYV;
	fmt.width = WIDTH1;
	fmt.height = HEIGHT1;
	fmt.pitch = WIDTH1 * 2;

	ret = video_set_format(jpeg_dev, VIDEO_EP_IN, &fmt);
	if (ret) {
		printk("Failed to set IN format: %d\n", ret);
		return 0;
	}

	/* Set output format */
	memset(&fmt, 0, sizeof(fmt));
	fmt.pixelformat = VIDEO_PIX_FMT_JPEG;
	fmt.width = WIDTH1;
	fmt.height = HEIGHT1;

	ret = video_set_format(jpeg_dev, VIDEO_EP_OUT, &fmt);
	if (ret) {
		printk("Failed to set OUT format: %d\n", ret);
		return 0;
	}

	memset(&out_buf, 0, sizeof(out_buf));
	struct video_buffer *donebuf = NULL;
	struct video_buffer *doneinbuf = NULL;
	static struct video_buffer inbuf[10];
	static struct video_buffer outbuf[10];
	for (uint8_t i = 0; i < queue_size; i++) {
		inbuf[i].buffer = test_352x288;
		inbuf[i].size = sizeof(test_352x288);
		outbuf[i].buffer = jpeg_out + i * 1024;
		outbuf[i].size = sizeof(jpeg_out);
	}

	/* Enqueue pair of buffers */
	for (uint8_t i = 0; i < queue_size; i++) {

		ret = video_enqueue(jpeg_dev, VIDEO_EP_IN, &inbuf[i]);
		if (ret) {
			printk("Enqueue input buffer %d error: %d\n", i, ret);
		}
		ret = video_enqueue(jpeg_dev, VIDEO_EP_OUT, &outbuf[i]);
		if (ret) {
			printk("Enqueue output buffer %d error: %d\n", i, ret);
		}
	}

	/* Start encoder */
	ret = video_stream_start(jpeg_dev);
	printk("Encoding %dx%d ...\n", WIDTH1, HEIGHT1);
	if (ret) {
		printk("Start video stream failed: %d\n", ret);
		return 0;
	}

	/* Wait encoder result */
	for (uint8_t i = 0; i < queue_size; i++) {
		ret = video_dequeue(jpeg_dev, VIDEO_EP_OUT, &donebuf, K_MSEC(50));
		if (ret) {
			printk("Dequeue output buffer %d error: %d (%s)\n", i, ret, strerror(-ret));
		}
		if (ret == 0 && donebuf != NULL) {
			printk("%d JPEG encoded OK — size = %u bytes, %d ms\n", i,
			       outbuf[i].bytesused, outbuf[i].timestamp - inbuf[i].timestamp);
		}
		dump_hex(outbuf[i].buffer, 16);
		dump_hex(outbuf[i].buffer + 800, 16);
		printk("\r\n");
		/* Dequeue input buffer for flush fifo */
		ret = video_dequeue(jpeg_dev, VIDEO_EP_IN, &doneinbuf, K_MSEC(50));
		if (ret) {
			printk("Dequeue input buffer %d error: %d (%s)\n", i, ret, strerror(-ret));
		}
	}

	printk("\r\n");
	/* Check if output fifos is empty */
	ret = video_dequeue(jpeg_dev, VIDEO_EP_OUT, &done_buf2, K_MSEC(50));
	if (ret) {
		printk("Dequeue empty VIDEO_EP_OUT, ret %d (%s) OK\n", ret, strerror(-ret));
	}
	ret = video_dequeue(jpeg_dev, VIDEO_EP_IN, &done_buf2, K_MSEC(50));
	if (ret) {
		printk("Dequeue empty VIDEO_EP_IN, ret %d (%s) OK\n", ret, strerror(-ret));
	}

	/* Start streaming again */
	ret = video_stream_start(jpeg_dev);
	printk("Start streaming again %d (%s) OK\n", ret, strerror(-ret));

	ret = video_set_format(jpeg_dev, VIDEO_EP_OUT, &fmt);
	printk("Failed to set OUT format while streaming: %d (%s) OK\n", ret, strerror(-ret));

	ret = video_stream_stop(jpeg_dev);
	if (ret) {
		printk("Stop video stream failed: %d\n", ret);
	}

	printk("\r\nDone\n");

	return 0;
}