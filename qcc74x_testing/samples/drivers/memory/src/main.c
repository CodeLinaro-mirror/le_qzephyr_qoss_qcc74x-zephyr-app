/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/mem_stats.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/cache.h>
#include <zephyr/net_buf.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <compiler/compiler_ld.h> // qcom hal

/* Known section name if psram is enabled */
/*
0x00000000a8063ee0                __psram_noinit_data_start__ = .
0x00000000a8063ee0                __psram_noinit_data_end__ = .
0x00000000a8063ee0                __psram_heap_base = .
0x00000000a8063ee0                __psram_heap_end = .
0x00000000a0174fd0                __psram_data_load_addr = LOADADDR (_PSRAM_DATA_NAME)
0x00000000a8063ee0                __psram_data_start__ = .
0x00000000a8063ee0                __psram_data_end__ = .
*/
extern uint8_t __psram_noinit_data_start__[] __attribute__((weak));
extern uint8_t __psram_noinit_data_end__[] __attribute__((weak));
extern uint8_t __psram_heap_base[] __attribute__((weak));
extern uint8_t __psram_heap_end[] __attribute__((weak));

extern uint8_t __psram_data_load_addr[] __attribute__((weak));
extern uint8_t __psram_data_start__[] __attribute__((weak));
extern uint8_t __psram_data_end__[] __attribute__((weak));

/*
libwireless.a:soc_multi_mem.c.obj:00000000 B my_heap1
libwireless.a:soc_multi_mem.c.obj:00000000 B my_heap2
libwireless.a:soc_multi_mem.c.obj:00000000 T soc_multi_heap_init
libwireless.a:soc_multi_mem.c.obj:00000000 T soc_multi_heap_stats
libwireless.a:soc_multi_mem.c.obj:00000000 T soc_print_multi_heap_info
libwireless.a:soc_multi_mem.c.obj:         U sys_heap_aligned_alloc
libwireless.a:soc_multi_mem.c.obj:         U sys_heap_init
libwireless.a:soc_multi_mem.c.obj:         U sys_heap_print_info
libwireless.a:soc_multi_mem.c.obj:         U sys_heap_runtime_stats_get
libwireless.a:soc_multi_mem.c.obj:         U sys_multi_heap_add_heap
libwireless.a:soc_multi_mem.c.obj:         U sys_multi_heap_aligned_alloc
libwireless.a:soc_multi_mem.c.obj:         U sys_multi_heap_aligned_realloc
libwireless.a:soc_multi_mem.c.obj:         U sys_multi_heap_free
libwireless.a:soc_multi_mem.c.obj:         U sys_multi_heap_init
*/

static uint8_t __noinit my_noinit_buffer[128];
K_HEAP_DEFINE(my_heap, 128);
#ifdef CONFIG_PSRAM
NET_BUF_POOL_DEFINE(tx_pool, 64, 64, 0, NULL);
#endif

static uint8_t __nocache my_nocache_buffer[128];
static uint8_t __nocache_noinit my_nocache_noinit_buffer[128];
K_HEAP_DEFINE_NOCACHE(my_nocache_heap, 128);

void psram_sections_info()
{
}

#define TEST_ALLOC_SIZE 1024 /* 1KB */

#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
static struct sys_memory_stats stats;
struct sys_heap system_heap;
void print_heap_stats(struct sys_memory_stats *stats, const char *heap_name)
{
	if (stats) {
		printk("%s Stats: Allocated: %zu bytes, Free: %zu bytes, Max Allocated: %zu "
		       "bytes\n",
		       heap_name, stats->allocated_bytes, stats->free_bytes,
		       stats->max_allocated_bytes);
	} else {
		printk("%s Stats: Not available\n", heap_name);
	}
}
#endif

void test_sys_heap(void)
{
#ifdef CONFIG_KERNEL_MEM_POOL
	printk("System Heap: CONFIG Defined\n");

#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
	int ret = sys_heap_runtime_stats_get(&system_heap, &stats);
	if (ret == 0) {
		print_heap_stats(&stats, "System Heap");
	} else {
		printk("System Heap Stats: Failed to retrieve (error %d)\n", ret);
	}
#endif

	/* Check if system heap is initialized by attempting allocation */
	void *ptr = k_malloc(TEST_ALLOC_SIZE);
	if (ptr) {
		printk("System Heap: Initialized, allocated %d bytes at %p\n", TEST_ALLOC_SIZE,
		       ptr);

		/* Get and print heap statistics */
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
		int ret = sys_heap_runtime_stats_get(&system_heap, &stats);
		if (ret == 0) {
			print_heap_stats(&stats, "System Heap");
		} else {
			printk("System Heap Stats: Failed to retrieve (error %d)\n", ret);
		}
#endif
		k_free(ptr);
	} else {
		printk("System Heap: Not initialized or allocation failed\n");
	}
#else
	printk("System Heap: Not defined\n");
#endif
}

#if 0
void test_multi_heap(void)
{
#ifdef CONFIG_HEAP_MULTI_HEAP
    printk("Multi Heap: Defined\n");

    /* Example: Define a static heap (replace with your actual heap) */
    K_HEAP_DEFINE(test_heap, 4096); /* 4KB heap */
    struct k_heap *heap = &test_heap;

    void *ptr = k_heap_alloc(heap, TEST_ALLOC_SIZE, K_NO_WAIT);
    if (ptr) {
        printk("Multi Heap: Initialized, allocated %d bytes at %p\n", TEST_ALLOC_SIZE, ptr);

        /* Note: Multi-heap stats API is not standardized; use custom logic if available */
        printk("Multi Heap Stats: Not supported in this example\n");

        k_heap_free(heap, ptr);
    } else {
        printk("Multi Heap: Not initialized or allocation failed\n");
    }
#else
    printk("Multi Heap: Not defined\n");
#endif
}
#endif

#define REGION_SIZE 4096
static uint8_t buffer[REGION_SIZE] __aligned(64); // Ensure proper alignment

void test_shared_multi_heap(void)
{
#ifdef CONFIG_SHARED_MULTI_HEAP
	printk("Shared Multi Heap: CONFIG Defined\n");

	int ret = shared_multi_heap_pool_init();
	if (ret) {
		printk("Shared Multi Heap  initialized already %s !\n", strerror(ret));
	} else {
		printk("Shared Multi Heap  initialized only now < %s > !\n", strerror(ret));

		struct shared_multi_heap_region region;
		// Initialize the region structure
		region.addr = (uintptr_t)buffer;      // Address of the buffer
		region.size = REGION_SIZE;            // Size of the region
		region.attr = SMH_REG_ATTR_CACHEABLE; // Set cacheable attribute
		shared_multi_heap_add(&region, NULL);
		ret = 1;
	}

	if (ret) {
		void *ptr =
			shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL, 8, TEST_ALLOC_SIZE);
		if (ptr) {
			printk("Shared Multi Heap: SMH_REG_ATTR_EXTERNAL, allocated %d bytes at "
			       "%p\n",
			       TEST_ALLOC_SIZE, ptr);

			shared_multi_heap_free(ptr);
		} else {
			printk("Shared Multi Heap: SMH_REG_ATTR_EXTERNAL Allocation failed !\n");
		}

		void *ptr1 =
			shared_multi_heap_aligned_alloc(SMH_REG_ATTR_CACHEABLE, 8, TEST_ALLOC_SIZE);
		if (ptr1) {
			printk("Shared Multi Heap: SMH_REG_ATTR_CACHEABLE, allocated %d bytes at "
			       "%p\n",
			       TEST_ALLOC_SIZE, ptr1);

			shared_multi_heap_free(ptr1);
		} else {
			printk("Shared Multi Heap: SMH_REG_ATTR_CACHEABLE failed !\n");
		}

		void *ptr2 = shared_multi_heap_aligned_alloc(SMH_REG_ATTR_NON_CACHEABLE, 8,
							     TEST_ALLOC_SIZE);
		if (ptr2) {
			printk("Shared Multi Heap: SMH_REG_ATTR_NON_CACHEABLE, allocated %d bytes "
			       "at %p\n",
			       TEST_ALLOC_SIZE, ptr2);

			shared_multi_heap_free(ptr2);
		} else {
			printk("Shared Multi Heap: SMH_REG_ATTR_NON_CACHEABLE failed !\n");
		}

		if (!ptr && !ptr1 && !ptr2) {
			printk("Shared Multi Heap: initialized but no free space or no any regions "
			       "are not added !\n");
		}
	} else {
		printk("Shared Multi Heap: Not initialized !\n");
	}
#else
	printk("Shared Multi Heap: Not defined !\n");
#endif
}

void test_noinit_alloc()
{
	printk("my_noinit_buffer at %p\n", (void *)my_noinit_buffer);
	void *buffer = k_heap_alloc(&my_heap, 128, K_MSEC(100));
	printk("K_HEAP_DEFINE at %p\n", (void *)&buffer);
#ifdef CONFIG_PSRAM
	struct net_buf *buf = net_buf_alloc(&tx_pool, K_MSEC(100));
	printk("NET_BUF_POOL_DEFINE at %p\n", (void *)buf);
#endif
}

void test_nocache_alloc()
{
	printk("my_nocache_buffer at %p\n", (void *)my_nocache_buffer);
	printk("my_nocache_noinit_buffer at %p\n", (void *)my_nocache_noinit_buffer);
	void *buffer = k_heap_alloc(&my_nocache_heap, 64, K_MSEC(100));
	printk("K_HEAP_DEFINE_NOCACHE at %p\n", (void *)&buffer);
}

#define BUFFER_SIZE (64 * 1024)
#define NUM_COPIES  500
static uint8_t src_buffer[BUFFER_SIZE] __aligned(64);
static uint8_t dst_buffer[BUFFER_SIZE] __aligned(64);

void memory_copy_speed_test()
{
	printk("Memory Copy Speed Test ...\n");

	// Initialize source buffer with some data
	memset(src_buffer, 0xAA, BUFFER_SIZE);

	// Start timing
	uint64_t start_cycles = k_cycle_get_64();

	// Perform copy operations
	for (int i = 0; i < NUM_COPIES; i++) {
#if 1
		extern void *arch_memcpy(void *d, const void *s, size_t n);
		extern void *arch_memcpy_fast(void *d, const void *s, size_t n);
		arch_memcpy_fast(dst_buffer, src_buffer, BUFFER_SIZE);
		arch_memcpy_fast(src_buffer, dst_buffer, BUFFER_SIZE);
#else
		memcpy(dst_buffer, src_buffer, BUFFER_SIZE);
		memcpy(src_buffer, dst_buffer, BUFFER_SIZE);
#endif
	}

	// End timing
	uint64_t end_cycles = k_cycle_get_64();

	// Calculate time in seconds
	uint64_t cycles = end_cycles - start_cycles;
	uint32_t freq_hz = sys_clock_hw_cycles_per_sec();
	double time_seconds = (double)cycles / freq_hz;

	// Calculate total data copied (in bytes)
	size_t total_bytes = BUFFER_SIZE * NUM_COPIES * 2;

	// Calculate speed in MB/s (1 MB = 1024 * 1024 bytes)
	double speed_mbps = (total_bytes / (1024.0 * 1024.0)) / time_seconds;

	// Print results
	printk("Memory Copy Speed Test Results:\n");
	printk("  Src buffer size: %zu bytes (%dKB) at %p\n", BUFFER_SIZE, BUFFER_SIZE / 1024,
	       src_buffer);
	printk("  Dst buffer size: %zu bytes (%dKB) at %p\n", BUFFER_SIZE, BUFFER_SIZE / 1024,
	       dst_buffer);
	printk("  Number of copies: %d\n", NUM_COPIES * 2);
	printk("  Total data copied: %zu bytes\n", total_bytes);
	printk("  Time taken: %.6f seconds\n", time_seconds);
	printk("  Copy speed: %.2f MB/s\n", speed_mbps);

	// Verify data
	if (memcmp(src_buffer, dst_buffer, BUFFER_SIZE) == 0) {
		printk("Data verification successful\n");
	} else {
		printk("Data verification failed\n");
	}
}

#define BUFFER1_SIZE (64 * 1024)
#define NUM_COPIES1  500
#define ATTR_NOCACHE ATTR_NOCACHE_NOINIT_RAM_SECTION
static uint8_t src_buffer1[BUFFER1_SIZE] __aligned(64) ATTR_NOCACHE;
static uint8_t dst_buffer1[BUFFER1_SIZE] __aligned(64) ATTR_NOCACHE;

void noinit_memory_copy_speed_test()
{
	printk("Noinit memory Copy Speed Test ...\n");

	// Initialize source buffer with some data
	memset(src_buffer1, 0xAA, BUFFER1_SIZE);

	// Start timing
	uint64_t start_cycles = k_cycle_get_64();

	// Perform copy operations
	for (int i = 0; i < NUM_COPIES1; i++) {
#if 1
		extern void *arch_memcpy(void *d, const void *s, size_t n);
		extern void *arch_memcpy_fast(void *d, const void *s, size_t n);
		arch_memcpy_fast(dst_buffer1, src_buffer1, BUFFER1_SIZE);
		arch_memcpy_fast(src_buffer1, dst_buffer1, BUFFER1_SIZE);
#else
		memcpy(dst_buffer1, src_buffer1, BUFFER1_SIZE);
		memcpy(src_buffer1, dst_buffer1, BUFFER1_SIZE);
#endif
	}

	// End timing
	uint64_t end_cycles = k_cycle_get_64();

	// Calculate time in seconds
	uint64_t cycles = end_cycles - start_cycles;
	uint32_t freq_hz = sys_clock_hw_cycles_per_sec();
	double time_seconds = (double)cycles / freq_hz;

	// Calculate total data copied (in bytes)
	size_t total_bytes = BUFFER1_SIZE * NUM_COPIES1 * 2;

	// Calculate speed in MB/s (1 MB = 1024 * 1024 bytes)
	double speed_mbps = (total_bytes / (1024.0 * 1024.0)) / time_seconds;

	// Print results
	printk("Noinit memory Copy Speed Test Results:\n");
	printk("  Src buffer size: %zu bytes (%dKB) at %p\n", BUFFER1_SIZE, BUFFER1_SIZE / 1024,
	       src_buffer1);
	printk("  Dst buffer size: %zu bytes (%dKB) at %p\n", BUFFER1_SIZE, BUFFER1_SIZE / 1024,
	       dst_buffer1);
	printk("  Number of copies: %d\n", NUM_COPIES1 * 2);
	printk("  Total data copied: %zu bytes\n", total_bytes);
	printk("  Time taken: %.6f seconds\n", time_seconds);
	printk("  Copy speed: %.2f MB/s\n", speed_mbps);

	// Verify data
	if (memcmp(src_buffer1, dst_buffer1, BUFFER1_SIZE) == 0) {
		printk("Data verification successful\n");
	} else {
		printk("Data verification failed\n");
	}
}

#define NUM_TRANSFERS 500
#define DMA_DEVICE    DT_NODELABEL(dma) // Adjust for your board
#define DMA_CHANNEL   0                 // Adjust based on your DMA controller
#undef BUFFER_SIZE
#define BUFFER_SIZE 65536

void dma_callback_x(const struct device *dev, void *user_data, uint32_t channel, int status)
{
	if (status != 0) {
		printk("DMA transfer error: %d %s\n", status, strerror(-status));
	}
}

void dma_mem_to_mem_speed_test(void)
{
	printk("Memory-to-Memory DMA Speed Test ...\n");

	const struct device *dma = DEVICE_DT_GET(DMA_DEVICE);
	if (!device_is_ready(dma)) {
		printk("DMA device not ready\n");
		return;
	}

	// Initialize source buffer with some data
	memset(src_buffer, 0xAA, BUFFER_SIZE);
	memset(dst_buffer, 0x55, BUFFER_SIZE);

	// Configure DMA channel for memory-to-memory
	struct dma_config dma_cfg = {0};

	dma_cfg.dma_slot = 0;
	dma_cfg.channel_direction = MEMORY_TO_MEMORY;
	dma_cfg.source_data_size = 4;    // 32-bit transfers
	dma_cfg.dest_data_size = 4;      // 32-bit transfers
	dma_cfg.source_burst_length = 4; // Adjust based on hardware
	dma_cfg.dest_burst_length = 4;   // Adjust based on hardware
	dma_cfg.block_count = 0;
	dma_cfg.head_block = NULL;
	dma_cfg.dma_callback = dma_callback_x;
	dma_cfg.complete_callback_en = 1;
	dma_cfg.error_callback_dis = 0;

	int ret = dma_config(dma, DMA_CHANNEL, &dma_cfg);
	if (ret != 0) {
		printk("DMA configuration failed: %d %s\n", ret, strerror(-ret));
		return;
	}

	// Start timing
	uint64_t start_cycles = k_cycle_get_64();

	// Perform DMA transfers
	for (int i = 0; i < NUM_TRANSFERS; i++) {

		// Reload DMA block configuration
		ret = dma_reload(dma, DMA_CHANNEL, (uint32_t)src_buffer, (uint32_t)dst_buffer,
				 BUFFER_SIZE);
		if (ret != 0) {
			printk("DMA %d reload failed: %d %s", i, ret, strerror(-ret));
			return;
		}
		// DMA driver handle cache inside
		ret = dma_start(dma, DMA_CHANNEL);
		if (ret != 0) {
			printk("DMA %d start failed: %d %s\n", i, ret, strerror(-ret));
			return;
		}
		// Wait for transfer completion (polling for simplicity)
		struct dma_status status;
		do {
			dma_get_status(dma, DMA_CHANNEL, &status);
		} while (status.busy);
	}

	// End timing
	uint64_t end_cycles = k_cycle_get_64();

	// Calculate time and speed
	uint64_t cycles = end_cycles - start_cycles;
	uint32_t freq_hz = sys_clock_hw_cycles_per_sec();
	double time_seconds = (double)cycles / freq_hz;
	size_t total_bytes = BUFFER_SIZE * NUM_TRANSFERS;
	double speed_mbps = (total_bytes / (1024.0 * 1024.0)) / time_seconds;

	// Print results
	printk("Memory-to-Memory DMA Speed Test Results:\n");
	printk("  Src buffer size: %zu bytes (%dKB) at %p\n", BUFFER_SIZE, BUFFER_SIZE / 1024,
	       src_buffer);
	printk("  Dst buffer size: %zu bytes (%dKB) at %p\n", BUFFER_SIZE, BUFFER_SIZE / 1024,
	       dst_buffer);
	printk("  Number of transfers: %d\n", NUM_TRANSFERS);
	printk("  Total data transferred: %zu bytes\n", total_bytes);
	printk("  Time taken: %.6f seconds\n", time_seconds);
	printk("  DMA copy speed: %.2f MB/s\n", speed_mbps);

	// Verify data
	if (memcmp(src_buffer, dst_buffer, BUFFER_SIZE) == 0) {
		printk("Data verification successful\n");
	} else {
		printk("Data verification failed\n");
	}
}

#define DMA_CHANNEL1 1

void noinit_dma_mem_to_mem_speed_test(void)
{
	printk("Noinit Memory-to-Memory DMA Speed Test ...\n");

	const struct device *dma = DEVICE_DT_GET(DMA_DEVICE);
	if (!device_is_ready(dma)) {
		printk("DMA device not ready\n");
		return;
	}

	// Initialize source buffer with some data
	memset(src_buffer1, 0xAA, BUFFER1_SIZE);
	memset(dst_buffer1, 0x55, BUFFER1_SIZE);

	// Configure DMA channel for memory-to-memory
	struct dma_config dma_cfg = {0};

	dma_cfg.dma_slot = 0;
	dma_cfg.channel_direction = MEMORY_TO_MEMORY;
	dma_cfg.source_data_size = 4;    // 32-bit transfers
	dma_cfg.dest_data_size = 4;      // 32-bit transfers
	dma_cfg.source_burst_length = 4; // Adjust based on hardware
	dma_cfg.dest_burst_length = 4;   // Adjust based on hardware
	dma_cfg.block_count = 0;
	dma_cfg.head_block = NULL;
	dma_cfg.dma_callback = dma_callback_x;
	dma_cfg.complete_callback_en = 1;
	dma_cfg.error_callback_dis = 0;

	int ret = dma_config(dma, DMA_CHANNEL1, &dma_cfg);
	if (ret != 0) {
		printk("DMA configuration failed: %d %s\n", ret, strerror(-ret));
		return;
	}

	// Start timing
	uint64_t start_cycles = k_cycle_get_64();

	// Perform DMA transfers
	for (int i = 0; i < NUM_TRANSFERS; i++) {

		// Reload DMA block configuration
		ret = dma_reload(dma, DMA_CHANNEL1, (uint32_t)src_buffer1, (uint32_t)dst_buffer1,
				 BUFFER1_SIZE);
		if (ret != 0) {
			printk("DMA %d reload failed: %d %s", i, ret, strerror(-ret));
			return;
		}

		// DMA driver handle cache inside
		ret = dma_start(dma, DMA_CHANNEL1);
		if (ret != 0) {
			printk("DMA %d start failed: %d %s\n", i, ret, strerror(-ret));
			return;
		}
		// Wait for transfer completion (polling for simplicity)
		struct dma_status status;
		do {
			dma_get_status(dma, DMA_CHANNEL1, &status);
		} while (status.busy);
	}

	// End timing
	uint64_t end_cycles = k_cycle_get_64();

	// Calculate time and speed
	uint64_t cycles = end_cycles - start_cycles;
	uint32_t freq_hz = sys_clock_hw_cycles_per_sec();
	double time_seconds = (double)cycles / freq_hz;
	size_t total_bytes = BUFFER1_SIZE * NUM_TRANSFERS;
	double speed_mbps = (total_bytes / (1024.0 * 1024.0)) / time_seconds;

	// Print results
	printk("Noinit Memory-to-Memory DMA Speed Test Results:\n");
	printk("  Src buffer size: %zu bytes (%dKB) at %p\n", BUFFER1_SIZE, BUFFER1_SIZE / 1024,
	       src_buffer1);
	printk("  Dst buffer size: %zu bytes (%dKB) at %p\n", BUFFER1_SIZE, BUFFER1_SIZE / 1024,
	       dst_buffer1);
	printk("  Number of transfers: %d\n", NUM_TRANSFERS);
	printk("  Total data transferred: %zu bytes\n", total_bytes);
	printk("  Time taken: %.6f seconds\n", time_seconds);
	printk("  DMA copy speed: %.2f MB/s\n", speed_mbps);

	// Verify data
	if (memcmp(src_buffer1, dst_buffer1, BUFFER1_SIZE) == 0) {
		printk("Data verification successful\n");
	} else {
		printk("Data verification failed\n");
	}
}

#define DMA_CHANNEL2 2

void noinit_psram_dma_mem_to_mem_speed_test(void)
{
	printk("Noinit/psram  Memory-to-Memory DMA Speed Test ...\n");

	const struct device *dma = DEVICE_DT_GET(DMA_DEVICE);
	if (!device_is_ready(dma)) {
		printk("DMA device not ready\n");
		return;
	}

	// Initialize source buffer with some data
	memset(src_buffer1, 0xAA, BUFFER1_SIZE);
	memset(dst_buffer, 0x55, BUFFER1_SIZE);

	// Configure DMA channel for memory-to-memory
	struct dma_config dma_cfg = {0};

	dma_cfg.dma_slot = 0;
	dma_cfg.channel_direction = MEMORY_TO_MEMORY;
	dma_cfg.source_data_size = 4;    // 32-bit transfers
	dma_cfg.dest_data_size = 4;      // 32-bit transfers
	dma_cfg.source_burst_length = 4; // Adjust based on hardware
	dma_cfg.dest_burst_length = 4;   // Adjust based on hardware
	dma_cfg.block_count = 0;
	dma_cfg.head_block = NULL;
	dma_cfg.dma_callback = dma_callback_x;
	dma_cfg.complete_callback_en = 1;
	dma_cfg.error_callback_dis = 0;

	int ret = dma_config(dma, DMA_CHANNEL2, &dma_cfg);
	if (ret != 0) {
		printk("DMA configuration failed: %d %s\n", ret, strerror(-ret));
		return;
	}

	// Start timing
	uint64_t start_cycles = k_cycle_get_64();

	// Perform DMA transfers
	for (int i = 0; i < NUM_TRANSFERS; i++) {

		// Reload DMA block configuration
		ret = dma_reload(dma, DMA_CHANNEL2, (uint32_t)src_buffer1, (uint32_t)dst_buffer,
				 BUFFER1_SIZE);
		if (ret != 0) {
			printk("DMA %d reload failed: %d %s", i, ret, strerror(-ret));
			return;
		}

		// DMA driver handle cache inside
		ret = dma_start(dma, DMA_CHANNEL2);
		if (ret != 0) {
			printk("DMA %d start failed: %d %s\n", i, ret, strerror(-ret));
			return;
		}
		// Wait for transfer completion (polling for simplicity)
		struct dma_status status;
		do {
			dma_get_status(dma, DMA_CHANNEL2, &status);
		} while (status.busy);
	}

	// End timing
	uint64_t end_cycles = k_cycle_get_64();

	// Calculate time and speed
	uint64_t cycles = end_cycles - start_cycles;
	uint32_t freq_hz = sys_clock_hw_cycles_per_sec();
	double time_seconds = (double)cycles / freq_hz;
	size_t total_bytes = BUFFER1_SIZE * NUM_TRANSFERS;
	double speed_mbps = (total_bytes / (1024.0 * 1024.0)) / time_seconds;

	// Print results
	printk("Noinit/psram Memory-to-Memory DMA Speed Test Results:\n");
	printk("  Src buffer size: %zu bytes (%dKB) at %p\n", BUFFER1_SIZE, BUFFER1_SIZE / 1024,
	       src_buffer1);
	printk("  Dst buffer size: %zu bytes (%dKB) at %p\n", BUFFER1_SIZE, BUFFER1_SIZE / 1024,
	       dst_buffer);
	printk("  Number of transfers: %d\n", NUM_TRANSFERS);
	printk("  Total data transferred: %zu bytes\n", total_bytes);
	printk("  Time taken: %.6f seconds\n", time_seconds);
	printk("  DMA copy speed: %.2f MB/s\n", speed_mbps);

	// Verify data
	if (memcmp(src_buffer1, dst_buffer, BUFFER1_SIZE) == 0) {
		printk("Data verification successful\n");
	} else {
		printk("Data verification failed\n");
	}
}

int main(void)
{
	printk("\nZephyr OS Memory usage test\n");
	printk("---------------------------------------\n");
	printk("\n");
	psram_sections_info();
	printk("\n");
	test_sys_heap();
	// test_multi_heap();
	printk("\n");
	test_shared_multi_heap();
	printk("\n");
	test_noinit_alloc();
	printk("\n");
	test_nocache_alloc();
	printk("\n");
	memory_copy_speed_test();
	printk("\n");
	noinit_memory_copy_speed_test();
	printk("\n");
	dma_mem_to_mem_speed_test();
	printk("\n");
	noinit_dma_mem_to_mem_speed_test();
	printk("\n");
	noinit_psram_dma_mem_to_mem_speed_test();
	printk("\n");
	printk("---------------------------------------\n");
	printk("Test Complete\n");

	return 0;
}
