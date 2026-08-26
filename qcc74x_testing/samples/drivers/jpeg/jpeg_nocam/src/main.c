/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */


#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video/jpeg_qcc74x.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/video/video.h>
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

/*
 * Dequeue a buffer from the fifo_out for the given direction.
 * Used for both normal dequeue and empty-fifo boundary tests.
 * The caller only needs to set vbuf->type; index is irrelevant for dequeue.
 */
static int dequeue_typed(struct video_buffer *vbuf, k_timeout_t timeout)
{
	struct video_buffer *p = vbuf;

	return video_dequeue(jpeg_dev, &p, timeout);
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
		.id  = VIDEO_CID_JPEG_COMPRESSION_QUALITY,
		.val = JPEG_QUALITY,
	};
	ret = video_set_ctrl(jpeg_dev, &quality_ctrl);
	if (ret) {
		printk("Failed to set JPEG quality: %d\n", ret);
	}

	/* =========================================================
	 * First image: 64×64
	 * ========================================================= */
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = VIDEO_BUF_TYPE_INPUT;
	fmt.pixelformat = VIDEO_PIX_FMT_YUYV;
	fmt.width = WIDTH; fmt.height = HEIGHT; fmt.pitch = WIDTH * 2;
	ret = video_set_format(jpeg_dev, &fmt);
	if (ret) { printk("Failed to set IN format: %d\n", ret); return 0; }

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = VIDEO_BUF_TYPE_OUTPUT;
	fmt.pixelformat = VIDEO_PIX_FMT_JPEG;
	fmt.width = WIDTH; fmt.height = HEIGHT;
	ret = video_set_format(jpeg_dev, &fmt);
	if (ret) { printk("Failed to set OUT format: %d\n", ret); return 0; }

	{
		uint16_t in_idx, out_idx;
		struct video_buffer in_ctrl  = {.type = VIDEO_BUF_TYPE_INPUT};
		struct video_buffer out_ctrl = {.type = VIDEO_BUF_TYPE_OUTPUT};
		struct video_buffer *done;

		video_import_buffer(test_64x64, sizeof(test_64x64), &in_idx);
		video_import_buffer(jpeg_out,   sizeof(jpeg_out),   &out_idx);
		in_ctrl.index = in_idx; out_ctrl.index = out_idx;

		video_enqueue(jpeg_dev, &out_ctrl);
		video_enqueue(jpeg_dev, &in_ctrl);

		video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_INPUT);
		video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
		printk("Encoding %dx%d ...\n", WIDTH, HEIGHT);

		done = &out_ctrl; done->type = VIDEO_BUF_TYPE_OUTPUT;
		ret = video_dequeue(jpeg_dev, &done, K_FOREVER);
		if (ret) { printk("Dequeue error: %d\n", ret); return 0; }
		if (done) {
			uint32_t t_out = done->timestamp;
			uint32_t sz    = done->bytesused;
			dump_hex(done->buffer, 16);
			dump_hex((uint8_t *)done->buffer + 800, 16);
			video_buffer_release(done);
			/* Dequeue input to get start timestamp for elapsed time */
			done = &in_ctrl; done->type = VIDEO_BUF_TYPE_INPUT;
			video_dequeue(jpeg_dev, &done, K_FOREVER);
			uint32_t elapsed = t_out - (done ? done->timestamp : t_out);
			if (done) { video_buffer_release(done); }
			printk("JPEG encoded OK — size = %u bytes, %u ms\n", sz, elapsed);
			printk("\n\n");
		} else {
			done = &in_ctrl; done->type = VIDEO_BUF_TYPE_INPUT;
			if (video_dequeue(jpeg_dev, &done, K_FOREVER) == 0 && done) {
				video_buffer_release(done);
			}
		}
		video_stream_stop(jpeg_dev, VIDEO_BUF_TYPE_INPUT);
		video_stream_stop(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
	}

	/* =========================================================
	 * Second image: 352×288
	 * ========================================================= */
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = VIDEO_BUF_TYPE_INPUT;
	fmt.pixelformat = VIDEO_PIX_FMT_YUYV;
	fmt.width = WIDTH1; fmt.height = HEIGHT1; fmt.pitch = WIDTH1 * 2;
	ret = video_set_format(jpeg_dev, &fmt);
	if (ret) { printk("Failed to set IN format: %d\n", ret); return 0; }

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = VIDEO_BUF_TYPE_OUTPUT;
	fmt.pixelformat = VIDEO_PIX_FMT_JPEG;
	fmt.width = WIDTH1; fmt.height = HEIGHT1;
	ret = video_set_format(jpeg_dev, &fmt);
	if (ret) { printk("Failed to set OUT format: %d\n", ret); return 0; }

	{
		uint16_t in_idx, out_idx;
		struct video_buffer in_ctrl  = {.type = VIDEO_BUF_TYPE_INPUT};
		struct video_buffer out_ctrl = {.type = VIDEO_BUF_TYPE_OUTPUT};
		struct video_buffer *done;

		video_import_buffer(test_352x288, sizeof(test_352x288), &in_idx);
		video_import_buffer(jpeg_out,     sizeof(jpeg_out),     &out_idx);
		in_ctrl.index = in_idx; out_ctrl.index = out_idx;

		video_enqueue(jpeg_dev, &out_ctrl);
		video_enqueue(jpeg_dev, &in_ctrl);

		video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_INPUT);
		video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
		printk("Encoding %dx%d ...\n", WIDTH1, HEIGHT1);

		done = &out_ctrl; done->type = VIDEO_BUF_TYPE_OUTPUT;
		ret = video_dequeue(jpeg_dev, &done, K_FOREVER);
		if (ret) { printk("Dequeue error: %d\n", ret); return 0; }
		if (done) {
			uint32_t t_out = done->timestamp;
			uint32_t sz    = done->bytesused;
			uint8_t *buf   = done->buffer;
			video_buffer_release(done);
			done = &in_ctrl; done->type = VIDEO_BUF_TYPE_INPUT;
			video_dequeue(jpeg_dev, &done, K_FOREVER);
			uint32_t elapsed = t_out - (done ? done->timestamp : t_out);
			if (done) { video_buffer_release(done); }
			printk("JPEG encoded OK — size = %u bytes, %u ms\n", sz, elapsed);
			dump_hex(buf, 16);
			dump_hex(buf + 800, 16);
			printk("\n\n");
		} else {
			done = &in_ctrl; done->type = VIDEO_BUF_TYPE_INPUT;
			if (video_dequeue(jpeg_dev, &done, K_FOREVER) == 0 && done) {
				video_buffer_release(done);
			}
		}
		video_stream_stop(jpeg_dev, VIDEO_BUF_TYPE_INPUT);
		video_stream_stop(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
	}

	/* =========================================================
	 * Third image: test_buf (memset pattern) + boundary tests
	 * ========================================================= */
	/* Format still 352×288 */
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = VIDEO_BUF_TYPE_OUTPUT;
	fmt.pixelformat = VIDEO_PIX_FMT_JPEG;
	fmt.width = WIDTH1; fmt.height = HEIGHT1;

	/* Fill with a uniform gray YUYV pattern (compressible, ensures JPEG fits
	 * in the 32KB output buffer). Timer-based rand in the reference also
	 * produces compressible data due to same-tick repetition. */
	memset(test_buf, 0x00, sizeof(test_buf));

	{
		uint16_t in_idx, out_idx;
		struct video_buffer in_ctrl  = {.type = VIDEO_BUF_TYPE_INPUT};
		struct video_buffer out_ctrl = {.type = VIDEO_BUF_TYPE_OUTPUT};
		struct video_buffer *done;

		video_import_buffer(test_buf,  sizeof(test_buf),  &in_idx);
		video_import_buffer(jpeg_out,  sizeof(jpeg_out),  &out_idx);
		in_ctrl.index = in_idx; out_ctrl.index = out_idx;

		video_enqueue(jpeg_dev, &out_ctrl);
		video_enqueue(jpeg_dev, &in_ctrl);

		video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_INPUT);
		video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
		printk("Encoding %dx%d ...\n", WIDTH1, HEIGHT1);

		/* Boundary: duplicate stream_start while encoding */
		ret = video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
		printk("Encoding start again %d (%s) OK\n", ret, strerror(-ret));

		/* Boundary: set_format while streaming (should fail -EBUSY) */
		ret = video_set_format(jpeg_dev, &fmt);
		printk("Failed to set OUT format when stream enabled: %d (%s) OK\n",
		       ret, strerror(-ret));

		done = &out_ctrl; done->type = VIDEO_BUF_TYPE_OUTPUT;
		ret = video_dequeue(jpeg_dev, &done, K_MSEC(3000));
		if (ret) { printk("Dequeue error: %d\n", ret); return 0; }
		if (done) {
			uint32_t t_out = done->timestamp;
			uint32_t sz    = done->bytesused;
			uint8_t *buf   = done->buffer;
			video_buffer_release(done);
			done = &in_ctrl; done->type = VIDEO_BUF_TYPE_INPUT;
			video_dequeue(jpeg_dev, &done, K_FOREVER);
			uint32_t elapsed = t_out - (done ? done->timestamp : t_out);
			if (done) { video_buffer_release(done); }
			printk("JPEG encoded OK — size = %u bytes, %u ms\n", sz, elapsed);
			dump_hex(buf, 16);
			dump_hex(buf + 800, 16);
			printk("\n\n");
		} else {
			done = &in_ctrl; done->type = VIDEO_BUF_TYPE_INPUT;
			if (video_dequeue(jpeg_dev, &done, K_FOREVER) == 0 && done) {
				video_buffer_release(done);
			}
		}
		video_stream_stop(jpeg_dev, VIDEO_BUF_TYPE_INPUT);
		video_stream_stop(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
	}

	/* Boundary: dequeue from empty fifo_out */
	{
		struct video_buffer tmp_out = {.type = VIDEO_BUF_TYPE_OUTPUT};
		struct video_buffer tmp_in  = {.type = VIDEO_BUF_TYPE_INPUT};

		ret = dequeue_typed(&tmp_out, K_MSEC(50));
		printk("Dequeue empty VIDEO_EP_OUT, ret %d (%s) OK\n",
		       ret, strerror(-ret));

		ret = dequeue_typed(&tmp_in, K_MSEC(50));
		printk("Dequeue empty VIDEO_EP_IN, ret %d (%s) OK\n",
		       ret, strerror(-ret));
	}

	/* Boundary: stream_start with empty FIFO → -EAGAIN
	 * INPUT start alone returns 0 (doesn't trigger start_codec yet).
	 * OUTPUT start triggers start_codec with empty FIFOs → -EAGAIN. */
	ret = video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_INPUT);
	if (ret == 0) {
		ret = video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
	}
	printk("Encoding with empty in/out fifos %d (%s) OK\n", ret, strerror(-ret));

	ret = video_stream_stop(jpeg_dev, VIDEO_BUF_TYPE_INPUT);
	if (ret == 0) {
		ret = video_stream_stop(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
	}
	printk("Stream stop %d (%s) OK\n", ret, strerror(-ret));

	/* =========================================================
	 * Batch: 9 pairs enqueued, encode sequentially
	 * ========================================================= */
	printk("\r\n\r\n");

	uint8_t queue_size = 9;

	{
		uint16_t in_idx[10], out_idx[10];
		struct video_buffer inbuf[10], outbuf[10];
		struct video_buffer *done;
		int sret;

		for (uint8_t i = 0; i < queue_size; i++) {
			video_import_buffer(test_352x288, sizeof(test_352x288), &in_idx[i]);
			video_import_buffer(jpeg_out,     sizeof(jpeg_out),     &out_idx[i]);
			inbuf[i].type  = VIDEO_BUF_TYPE_INPUT;
			inbuf[i].index = in_idx[i];
			outbuf[i].type  = VIDEO_BUF_TYPE_OUTPUT;
			outbuf[i].index = out_idx[i];
		}

		for (uint8_t i = 0; i < queue_size; i++) {
			sret = video_enqueue(jpeg_dev, &inbuf[i]);
			if (sret) {
				printk("Enqueue input %d error: %d\n", i, sret);
			}
			sret = video_enqueue(jpeg_dev, &outbuf[i]);
			if (sret) {
				printk("Enqueue output %d error: %d\n", i, sret);
			}
		}

		sret = video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_INPUT);
		if (sret == 0) {
			sret = video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
		}
		printk("Encoding %dx%d ...\n", WIDTH1, HEIGHT1);
		if (sret) {
			printk("Start video stream failed: %d\n", sret);
			return 0;
		}

		for (uint8_t i = 0; i < queue_size; i++) {
			done = &outbuf[i]; done->type = VIDEO_BUF_TYPE_OUTPUT;
			sret = video_dequeue(jpeg_dev, &done, K_MSEC(1000));
			if (sret) {
				printk("Dequeue output %d error: %d (%s)\n",
				       i, sret, strerror(-sret));
			}
			if (sret == 0 && done) {
				uint32_t t_out   = done->timestamp;
				uint32_t sz      = done->bytesused;
				uint8_t *buf     = done->buffer;
				video_buffer_release(done);
				/* Get input timestamp for elapsed */
				done = &inbuf[i]; done->type = VIDEO_BUF_TYPE_INPUT;
				sret = video_dequeue(jpeg_dev, &done, K_MSEC(100));
				uint32_t elapsed = t_out - (sret == 0 && done ? done->timestamp : t_out);
				if (sret == 0 && done) { video_buffer_release(done); }
				printk("%d JPEG encoded OK — size = %u bytes, %u ms\n",
				       i, sz, elapsed);
				dump_hex(buf, 16);
				dump_hex(buf + 800, 16);
				printk("\r\n");
			} else {
				done = &inbuf[i]; done->type = VIDEO_BUF_TYPE_INPUT;
				sret = video_dequeue(jpeg_dev, &done, K_MSEC(100));
				if (sret == 0 && done) {
					video_buffer_release(done);
				}
			}
		}

		/* Boundary: dequeue from now-empty fifo (stream still running) */
		{
			struct video_buffer tmp_out = {.type = VIDEO_BUF_TYPE_OUTPUT};
			struct video_buffer tmp_in  = {.type = VIDEO_BUF_TYPE_INPUT};

			ret = dequeue_typed(&tmp_out, K_MSEC(50));
			printk("Dequeue empty VIDEO_EP_OUT, ret %d (%s) OK\n",
			       ret, strerror(-ret));

			ret = dequeue_typed(&tmp_in, K_MSEC(50));
			printk("Dequeue empty VIDEO_EP_IN, ret %d (%s) OK\n",
			       ret, strerror(-ret));
		}

		/* Boundary: start again while stream still running → -EALREADY */
		ret = video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_INPUT);
		if (ret == 0) {
			ret = video_stream_start(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
		}
		printk("Start streaming again %d (%s) OK\n", ret, strerror(-ret));

		/* Boundary: set_format while streaming → -EBUSY */
		memset(&fmt, 0, sizeof(fmt));
		fmt.type = VIDEO_BUF_TYPE_OUTPUT;
		fmt.pixelformat = VIDEO_PIX_FMT_JPEG;
		fmt.width = WIDTH1; fmt.height = HEIGHT1;
		ret = video_set_format(jpeg_dev, &fmt);
		printk("Failed to set OUT format while streaming: %d (%s) OK\n",
		       ret, strerror(-ret));

		video_stream_stop(jpeg_dev, VIDEO_BUF_TYPE_INPUT);
		video_stream_stop(jpeg_dev, VIDEO_BUF_TYPE_OUTPUT);
	}

	printk("\r\nDone\n");
	return 0;
}
