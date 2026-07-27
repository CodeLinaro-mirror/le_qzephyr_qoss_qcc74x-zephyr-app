/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#define PSRAM_MEM_LOW 0xA8000000U
#define PSRAM_MEM_HIGH (PSRAM_MEM_LOW + 4 * 1024 * 1024)

#define FLASH_MEM_LOW 0xA0000000U
#define FLASH_MEM_HIGH (FLASH_MEM_LOW + 8 * 1024 * 1024)

#define PATTERN 0x11121314
#define PATTERN1 0xA1A2A3A4
#define PATTERN2 0xC1C2C3C4
#define PATTERN3 0xB1B2B3B4

/* Initialized data */
static uint32_t psram_data[4] __attribute__((section(".data")))
__attribute__((used)) = {0, PATTERN, 0, 0};

/* Zeroed data */
static uint32_t psram_bss[4] __attribute__((section(".bss"))) __attribute__((used));

/* Initialized rodata data */
static const uint32_t psram_rodata[4] __attribute__((section(".rodata")))
__attribute__((used)) = {0x0, PATTERN1, 0, 0};

/* Uninitialized data */
static uint32_t psram_noinit[4] __attribute__((section(".noinit"))) __attribute__((used));

static bool is_flash_ptr(const void *p)
{
	bool r = ((intptr_t)p >= FLASH_MEM_LOW && (intptr_t)p < FLASH_MEM_HIGH);
	return r;
}

static bool is_psram_ptr(const void *p)
{
	bool r = ((intptr_t)p >= PSRAM_MEM_LOW && (intptr_t)p < PSRAM_MEM_HIGH);
	return r;
}

int main(void)
{
	printk("\n");
	printk("PSRAM data allocation: %s\n", (is_psram_ptr(&psram_data[1])) ? "Passed" : "Failed");
	printk("PSRAM bss allocation: %s\n", (is_psram_ptr(&psram_bss[1])) ? "Passed" : "Failed");
	printk("PSRAM rodata allocation: %s\n",
		   (is_flash_ptr(&psram_rodata[1]) || is_psram_ptr(&psram_rodata[1])) ? "Passed" : "Failed");
	printk("PSRAM noinit allocation: %s\n",
		   (is_psram_ptr(&psram_noinit[1])) ? "Passed" : "Failed");
	uint32_t *psram_buff = k_malloc(16);
	if (psram_buff == NULL)
	{
		printk("Failed to allocate buffer in heap !\n");
	}
	else
	{
		printk("PSRAM heap allocation: %s\n",
			   (is_psram_ptr(&psram_buff[1])) ? "Passed" : "Failed");
	}
	printk("\n");
	printk("PSRAM data value: %s\n",
		   (psram_data[1] == PATTERN) ? "Passed" : "Failed");
	printk("PSRAM bss value: %s\n",
		   (psram_bss[1] == 0x00) ? "Passed" : "Failed");
	printk("PSRAM rodata value: %s\n",
		   (psram_rodata[1] == PATTERN1) ? "Passed" : "Failed");
	psram_noinit[1] = PATTERN2;
	printk("PSRAM noinit value: %s\n", (psram_noinit[1] == PATTERN2) ? "Passed" : "Failed");
	if (psram_buff != NULL)
	{
		psram_buff[1] = PATTERN3;
		printk("PSRAM heap value: %s\n", (psram_buff[1] == PATTERN3) ? "Passed" : "Failed");
		k_free(psram_buff);
	}

	return 0;
}
