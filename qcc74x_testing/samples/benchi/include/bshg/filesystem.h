/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <jsmn-extended.h>

// LITTLEFS
void bshg_dirname(const jsmn_json_t *filepath, jsmn_json_t *dirpath);
int create_directories(const jsmn_json_t *path);
int generate_file(const jsmn_json_t *path, uint32_t size);
int write_file(const jsmn_json_t *path, const jsmn_json_t *content);
int append_file(const jsmn_json_t *path, const jsmn_json_t *content);
int read_file(const jsmn_json_t *path, jsmn_json_t *result);
int remove_file(const jsmn_json_t *path);
int32_t get_filesize(const char *path);
void mount_littlefs();

// RAW FLASH
int benchi_flash_raw_read(uint32_t offset, uint32_t len, uint8_t *result);
int benchi_flash_raw_write(const jsmn_json_t *data, uint32_t offset);
int benchi_flash_raw_clean_write(const jsmn_json_t *data, uint32_t offset);
int benchi_flash_raw_generate(uint32_t offset, uint32_t size);
int benchi_flash_raw_clean_generate(uint32_t offset, uint32_t size);
int benchi_flash_erase(uint32_t offset, uint32_t size);
int benchi_flash_erase_full();
int benchi_flash_verify(uint32_t offset, uint32_t len, const uint8_t *expected);

// FLASH IFP
int benchi_flash_raw_write_ifp(const jsmn_json_t *data, uint32_t offset);
int benchi_flash_raw_write_ifp_fixed(const jsmn_json_t *size, const jsmn_json_t *offset);
