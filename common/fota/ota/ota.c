/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* QCC74x FOTA image core: partition, flash, integrity, and activation. */
#include "ota_internal.h"
#include "qcc74x_fota.h"
#include <errno.h>
#include <psa/crypto.h>
#include <qc7xx_common.h>
#include <qc7xx_flash.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#define PT0           0xe000U
#define PT1           0xf000U
#define PT_MAGIC      0x54504642U
#define FLASH_BLOCK   4096U
#define WRITE_BLOCK   32U
#define PROGRESS_STEP (64U * 1024U)

struct __packed pt_header {
	uint32_t magic;
	uint16_t version, count;
	uint32_t age, crc;
};

struct __packed pt_entry {
	uint8_t type, dev, active, name[9];
	uint32_t start[2], max[2], len, age;
};

struct __packed partition_table {
	struct pt_header header;
	struct pt_entry entries[16];
};

BUILD_ASSERT(sizeof(struct qcc74x_ota_header) == QCC74X_OTA_HEADER_SIZE, "bad OTA header");
BUILD_ASSERT(sizeof(struct pt_entry) == 36, "bad partition entry");

static struct {
	bool staged;
	int err;
	uint32_t bytes, target, total;
	uint8_t active;
} state;

static int flash_read(uint32_t a, void *b, size_t n)
{
	return qc7xx_flash_read(a, b, n) ? -EIO : 0;
}

static int flash_write(uint32_t a, const void *b, size_t n)
{
	return qc7xx_flash_write(a, (uint8_t *)b, n) ? -EIO : 0;
}

static int flash_erase(uint32_t a, size_t n)
{
	return qc7xx_flash_erase(a, n) ? -EIO : 0;
}

static uint32_t crc(const void *p, size_t n)
{
	return qc7xx_soft_crc32((void *)p, n);
}

static uint32_t *entries_crc(struct partition_table *p)
{
	return (uint32_t *)((uint8_t *)p->entries + p->header.count * sizeof(p->entries[0]));
}

static size_t table_length(const struct partition_table *p)
{
	return sizeof(p->header) + p->header.count * sizeof(p->entries[0]) + sizeof(uint32_t);
}

static bool table_valid(const struct partition_table *p)
{
	return p->header.magic == PT_MAGIC && p->header.count &&
	       p->header.count <= ARRAY_SIZE(p->entries) && p->header.crc == crc(&p->header, 12) &&
	       *entries_crc((struct partition_table *)p) ==
		       crc(p->entries, p->header.count * sizeof(p->entries[0]));
}

static int fw_entry(const struct partition_table *p)
{
	int i;

	for (i = 0; i < p->header.count; i++) {
		if (p->entries[i].type == 0 || !memcmp(p->entries[i].name, "FW", 2)) {
			return i;
		}
	}

	return -ENOENT;
}
static int load_partition(struct partition_table *p, struct qcc74x_ota_image *image)
{
	struct partition_table partition_a, partition_b;
	const struct partition_table *selected;
	int entry;

	if (flash_read(PT0, &partition_a, sizeof(partition_a)) ||
	    flash_read(PT1, &partition_b, sizeof(partition_b))) {
		return -EIO;
	}

	if (!table_valid(&partition_a) && !table_valid(&partition_b)) {
		return -EINVAL;
	}

	selected = (!table_valid(&partition_b) ||
		    (table_valid(&partition_a) && partition_a.header.age >= partition_b.header.age))
			   ? &partition_a
			   : &partition_b;

	memcpy(p, selected, sizeof(*p));
	entry = fw_entry(p);

	if (entry < 0 || p->entries[entry].active > 1) {
		return entry < 0 ? entry : -ENOENT;
	}

	image->active_slot = p->entries[entry].active;
	image->address = p->entries[entry].start[!image->active_slot];
	image->capacity = p->entries[entry].max[!image->active_slot];

	return 0;
}
static int erase_image(const struct qcc74x_ota_image *image)
{
	uint32_t off, n;
	printk("OTA: erase %u bytes from flash\n", image->capacity);

	for (off = 0; off < image->capacity; off += n) {
		n = MIN(FLASH_BLOCK, image->capacity - off);
		if (flash_erase(image->address + off, n)) {
			printk("OTA: erase flash failed\n");
			return -EIO;
		}

		/*
		if (((off + n) % PROGRESS_STEP) == 0 || off + n == image->capacity)
		  printk("OTA: erase %u%% (%u/%u bytes)\n",
			(off + n) * 100U / image->capacity, off + n, image->capacity);
		*/

		k_sleep(K_MSEC(1));
	}
	printk("OTA: erase done\n");
	return 0;
}

static int write_image(uint32_t address, const uint8_t *data, uint32_t length)
{
	uint32_t off = 0, n;

	while (off < length) {
		n = MIN(WRITE_BLOCK, length - off);
		if (flash_write(address + off, data + off, n)) {
			return -EIO;
		}
		// k_sleep(K_MSEC(1));
		off += n;
	}

	return 0;
}

static int digest(uint32_t address, uint32_t length, uint8_t out[32], uint8_t *buffer,
		  size_t buffer_size)
{
	psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
	psa_status_t status;
	uint32_t off = 0, n;
	size_t out_length = 0;
	int rc = 0;

	status = psa_crypto_init();
	if (status != PSA_SUCCESS) {
		printk("OTA: PSA crypto init failed: %d\n", status);
		return -EIO;
	}

	status = psa_hash_setup(&operation, PSA_ALG_SHA_256);
	if (status != PSA_SUCCESS) {
		printk("OTA: PSA SHA-256 setup failed: %d\n", status);
		return -EIO;
	}

	while (!rc && off < length) {
		n = MIN(MIN(FLASH_BLOCK, buffer_size), length - off);
		rc = flash_read(address + off, buffer, n);
		if (rc) {
			printk("OTA: SHA flash read failed at 0x%08x (%u bytes): %d\n",
			       address + off, n, rc);
		} else {
			status = psa_hash_update(&operation, buffer, n);
			if (status != PSA_SUCCESS) {
				printk("OTA: PSA SHA-256 update failed at %u: %d\n", off, status);
				rc = -EIO;
			}
		}
		off += n;
	}

	if (!rc) {
		status = psa_hash_finish(&operation, out, 32, &out_length);
		if (status != PSA_SUCCESS) {
			printk("OTA: PSA SHA-256 finish failed: %d\n", status);
			rc = -EIO;
		}
	}

	psa_hash_abort(&operation);
	return rc;
}

int qcc74x_ota_prepare(struct qcc74x_ota_image *image)
{
	struct partition_table table;
	int rc;

	if (!image) {
		return -EINVAL;
	}

	memset(&state, 0, sizeof(state));
	rc = load_partition(&table, image);

	if (rc) {
		return state.err = rc;
	}

	state.target = image->address;
	state.active = image->active_slot;

	printk("OTA: active FW slot=%u; pre-erasing inactive slot [0x%08x, 0x%08x) "
	       "before TCP connect\n",
	       image->active_slot, image->address, image->address + image->capacity);

	return state.err = erase_image(image);
}

int qcc74x_ota_validate_header(const struct qcc74x_ota_header *h,
			       const struct qcc74x_ota_image *image)
{
	return !h || !image || memcmp(h->magic, "QCC74X_OTA", 10) || memcmp(h->type, "RAW ", 4) ||
			       !h->length || h->length > image->capacity || (h->length % 32)
		       ? -EINVAL
		       : 0;
}

void qcc74x_ota_stage_begin(const struct qcc74x_ota_header *h)
{
	state.total = h->length;
}

int qcc74x_ota_write(const struct qcc74x_ota_image *image, uint32_t offset, const uint8_t *data,
		     uint32_t length)
{
	int rc;
	if (!image || !data || !length || offset > state.total || length > state.total - offset) {
		return -EINVAL;
	}
	rc = write_image(image->address + offset, data, length);
	if (!rc) {
		state.bytes = offset + length;
	}
	return rc;
}

int qcc74x_ota_verify(const struct qcc74x_ota_image *image, const struct qcc74x_ota_header *h,
		      uint8_t *buffer, size_t buffer_size)
{
	uint8_t sum[32];
	int rc;

	if (!image || !h || !buffer || buffer_size == 0U || state.bytes != h->length) {
		return -EINVAL;
	}

	rc = digest(image->address, h->length, sum, buffer, buffer_size);

	if (!rc && memcmp(sum, h->sha256, sizeof(sum))) {
		printk("OTA: SHA-256 mismatch\n");
		rc = -EBADMSG;
	}

	state.staged = !rc;
	return rc;
}

void qcc74x_ota_set_error(int error)
{
	state.err = error;
}

int qcc74x_ota_activate(void)
{
	struct partition_table p, a, b;
	struct qcc74x_ota_image image;
	uint32_t destination;
	int entry, rc;

	if (!state.staged) {
		return -EPERM;
	}

	rc = load_partition(&p, &image);
	if (rc || image.address != state.target) {
		return rc ? rc : -ESTALE;
	}

	entry = fw_entry(&p);
	if (entry < 0) {
		return entry;
	}
	p.entries[entry].active = !image.active_slot;
	p.entries[entry].len = state.bytes;
	p.entries[entry].age++;
	p.header.age++;
	p.header.crc = crc(&p.header, 12);
	*entries_crc(&p) = crc(p.entries, p.header.count * sizeof(p.entries[0]));

	if (flash_read(PT0, &a, sizeof(a)) || flash_read(PT1, &b, sizeof(b))) {
		return -EIO;
	}

	destination =
		(!table_valid(&b) || (table_valid(&a) && a.header.age >= b.header.age)) ? PT1 : PT0;

	rc = flash_erase(destination, FLASH_BLOCK);

	if (!rc) {
		rc = flash_write(destination, &p, table_length(&p));
	}

	if (!rc) {
		state.staged = false;
	}

	return rc;
}

int qcc74x_ota_rollback(void)
{
	struct partition_table p, a, b;
	struct qcc74x_ota_image image;
	uint32_t destination;
	int entry, rc;

	/* A staged image has not been activated, so switching slots is not rollback. */
	if (state.staged) {
		return -EBUSY;
	}

	rc = load_partition(&p, &image);
	if (rc) {
		return rc;
	}

	entry = fw_entry(&p);
	if (entry < 0) {
		return entry;
	}

	/* select the other FW slot in a new PT copy. */
	p.entries[entry].active = !image.active_slot;
	p.entries[entry].age++;
	p.header.age++;
	p.header.crc = crc(&p.header, 12);
	*entries_crc(&p) = crc(p.entries, p.header.count * sizeof(p.entries[0]));

	if (flash_read(PT0, &a, sizeof(a)) || flash_read(PT1, &b, sizeof(b))) {
		return -EIO;
	}

	/* Write the non-selected PT, retaining the selected valid copy until complete. */
	destination =
		(!table_valid(&b) || (table_valid(&a) && a.header.age >= b.header.age)) ? PT1 : PT0;

	rc = flash_erase(destination, FLASH_BLOCK);
	if (!rc) {
		rc = flash_write(destination, &p, table_length(&p));
	}

	return rc;
}

void qcc74x_ota_status(char *buffer, size_t length)
{
	snprintf(buffer, length, "bytes=%u/%u target=0x%08x staged=%u err=%d", state.bytes,
		 state.total, state.target, state.staged, state.err);
}

int qcc74x_ota_slot(char *buffer, size_t length)
{
	struct partition_table p;
	struct qcc74x_ota_image image;
	int rc, entry;

	if (!buffer || !length) {
		return -EINVAL;
	}

	rc = load_partition(&p, &image);

	if (rc) {
		return rc;
	}

	entry = fw_entry(&p);

	if (entry < 0) {
		return entry;
	}

	snprintf(buffer, length,
		 "active FW slot=%u [0x%08x, 0x%08x); inactive FW slot=%u [0x%08x, "
		 "0x%08x)",
		 image.active_slot, p.entries[entry].start[image.active_slot],
		 p.entries[entry].start[image.active_slot] +
			 p.entries[entry].max[image.active_slot],
		 !image.active_slot, image.address, image.address + image.capacity);
	return 0;
}
