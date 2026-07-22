/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <bshg/filesystem.h>

#include <zephyr/logging/log.h>
#include <BSHG_tracing.h>

LOG_MODULE_REGISTER(app);

extern void create_threads(void);
extern void webserver_main(void);

#ifndef CONFIG_ZTEST
int main(void)
{
	mount_littlefs();
	create_threads();
	webserver_main();
	BSHG_initTracing();
	return 0;
}
#endif
