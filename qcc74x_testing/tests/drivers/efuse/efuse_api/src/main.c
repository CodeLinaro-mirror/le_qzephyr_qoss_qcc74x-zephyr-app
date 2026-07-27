/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/syscon.h>
#include <zephyr/ztest.h>

ZTEST(efuse, test_size)
{
	const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(efuse));
	const size_t expected_size = DT_PROP(DT_NODELABEL(efuse), size);
	size_t size = 0U;

	zassert_ok(syscon_get_size(dev, &size));
	zassert_equal(size, expected_size, "size(%zu) != expected_size(%zu)", size,
		      expected_size);
}

ZTEST(efuse, test_out_of_bounds)
{
	const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(efuse));
	uint32_t val = 0U;

	zassert_equal(syscon_read_reg(dev, DT_PROP(DT_NODELABEL(efuse), size), &val), -EINVAL);
	zassert_equal(syscon_write_reg(dev, DT_REG_SIZE(DT_NODELABEL(efuse)), val), -ENOTSUP);
}

ZTEST(efuse, test_read)
{
	const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(efuse));
	const size_t size = DT_PROP(DT_NODELABEL(efuse), size);
	uint32_t val = 0U;

	for (size_t offset = 0; offset < size/sizeof(uint32_t); offset+=sizeof(uint32_t)) {
		zassert_ok(syscon_read_reg(dev, offset, &val));
	}
}

ZTEST(efuse, test_read_nonaligned)
{
	const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(efuse));
	uint32_t val = 0U;
	uint8_t val8[8] = {0U};

	/* Nonaligned access */
	zassert_equal(syscon_read_reg(dev, 2, &val), -EINVAL);
	zassert_equal(syscon_read_reg(dev, 0, (uint32_t *)&val8[1]), -EINVAL);
}

ZTEST(efuse, test_write)
{
	const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(efuse));
	uint32_t val = 0xFFFFFFFF;
	zassert_equal(syscon_write_reg(dev, 0, val), -ENOTSUP);
	zassert_equal(syscon_write_reg(dev, 2, val), -ENOTSUP);
}

ZTEST_SUITE(efuse, NULL, NULL, NULL, NULL, NULL);
