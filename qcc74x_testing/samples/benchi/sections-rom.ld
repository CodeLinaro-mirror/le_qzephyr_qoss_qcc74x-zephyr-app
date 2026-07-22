/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/linker/iterable_sections.h>

ITERABLE_SECTION_ROM(http_resource_desc_test_http_service, Z_LINK_ITERABLE_SUBALIGN)
ITERABLE_SECTION_ROM(http_resource_desc_test_https_service, Z_LINK_ITERABLE_SUBALIGN)
