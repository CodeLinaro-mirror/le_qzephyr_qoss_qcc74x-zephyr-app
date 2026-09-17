/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#include "qcc74x_fota.h"

#define OTA_FIXTURE_PATH "/lfs/zephyr.bin.ota"

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);

static struct fs_mount_t lfs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &storage,
	.storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
	.mnt_point = "/lfs",
};

static void *ota_fs_setup(void)
{
	int rc = fs_mount(&lfs_mnt);

	zassert_ok(rc, "mount /lfs failed: %d; flash littlefs.bin at 0x00400000", rc);
	return NULL;
}

ZTEST(qcc74x_ota_fs, test_stage_ota_image_from_littlefs)
{
	struct fs_dirent after;
	char status[128];
	int rc;

	printk("OTA FS test revision: bufsafe-no-slot-probe\n");
	rc = qcc74x_fota_stage_file(OTA_FIXTURE_PATH);
	zassert_ok(rc, "local file OTA stage failed: %d", rc);

	qcc74x_ota_status(status, sizeof(status));
	zassert_not_null(strstr(status, "staged=1"), "image was not staged: %s", status);

	/* The source fixture must survive erasing and writing the FW slot. */
	rc = fs_stat(OTA_FIXTURE_PATH, &after);
	zassert_ok(rc, "fixture disappeared after OTA: %d", rc);
	zassert_equal(after.type, FS_DIR_ENTRY_FILE, "fixture is not a file");
	zassert_true(after.size > 512U, "fixture is too small after OTA");

	/* Activate only after the staged image and its source fixture are valid. */
	rc = qcc74x_ota_activate();
	zassert_ok(rc, "local file OTA activation failed: %d", rc);
	printk("OTA test: activation complete; reset to boot the newly active slot\n");
}

ZTEST_SUITE(qcc74x_ota_fs, NULL, ota_fs_setup, NULL, NULL, NULL);
