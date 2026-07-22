/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>

#include <jsmn-extended.h>
#include <bshg/filesystem.h>

#include <stdlib.h>
#include <zephyr/syscalls/random.h>

#include <BSHG_tracing.h>

result_t process_msg_ll_file(const jsmn_json_t *json, json_msg *reply)
{
	if (json->data[0] != '[') {
		benchi_create_reply_msg(reply, 1, "Error: must be an array");
		return E_ERROR;
	}
	if (json->data[json->length - 1] != ']') {
		benchi_create_reply_msg(reply, 1, "Error: must be an array");
		return E_ERROR;
	}

	jsmn_json_t messages[30] = {};
	jsmn_get_array_items_ptrs(json->data, json->length, messages);

	jsmn_json_t action = {};
	jsmn_json_t path = {};
	jsmn_json_t data = {};
	jsmn_json_t size = {};
	jsmn_json_t offset = {};

	char read_content_buffer[256] = {0};
	jsmn_json_t read_content = {read_content_buffer, sizeof(read_content_buffer)};

	for (int i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
		if (!messages[i].data) {
			if (i == 0) {
				benchi_create_reply_msg(reply, 1, "Error: no messages specified");
			}
			break;
		}

		// mandatory properties
		if (jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"action", 6}},
						 &action)) {
			benchi_create_reply_msg(reply, 1, "Error: missing action");
			return E_ERROR; // Must be specified
		}

		// optional properties
		jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"path", 4}}, &path);
		jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"data", 4}}, &data);
		jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"size", 4}}, &size);
		jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"offset", 6}},
					     &offset);

		// modify mandatory fields
		if (jsmn_remove_quotes(&action)) {
			benchi_create_reply_msg(reply, 1, "Error: invalid action");
			return E_ERROR;
		}

		// modify optional fields
		if (path.length) {
			if (jsmn_remove_quotes(&path)) {
				benchi_create_reply_msg(reply, 1, "Error: invalid path");
				return E_ERROR;
			}
		}

		// Check for necessary fields
		if (strncmp(action.data, "raw_", 4) && !path.length) {
			benchi_create_reply_msg(reply, 1, "Error: missing path");
			return E_ERROR; // Must be specified
		}

		// Process according actions
		if (!strncmp(action.data, "read", 4)) {
			BSHG_setTracepoint_extended("read_file_start");
			int read_bytes = read_file(&path, &read_content);
			BSHG_setTracepoint_extended("read_done_%d", read_bytes);
			if (read_bytes >= 0) {
				benchi_create_reply_msg(reply, 0,
							"Successfully read file with %d bytes",
							read_bytes);
			} else {
				benchi_create_reply_msg(reply, 1, "Error: failed to read file");
				return E_ERROR;
			}
		} else if (!strncmp(action.data, "write", 5)) {
			if (jsmn_remove_quotes(&data)) {
				benchi_create_reply_msg(reply, 1, "Error: invalid data");
				return E_ERROR;
			}

			BSHG_setTracepoint_extended("write_file_%d", data.length);
			int written_bytes = write_file(&path, &data);
			BSHG_setTracepoint_extended("write_done");
			if (written_bytes >= 0) {
				benchi_create_reply_msg(reply, 0, "Successfully written %d bytes",
							data.length);
				return E_SUCCESS;
			} else {
				benchi_create_reply_msg(reply, 1, "Error: failed to write file");
				return E_ERROR;
			}
		} else if (!strncmp(action.data, "generate", 8)) {
			const uint32_t data_size = strtoll(size.data, NULL, 10);
			if (!data_size) {
				benchi_create_reply_msg(reply, 1, "Error: invalid size");
				return E_ERROR;
			}

			BSHG_setTracepoint_extended("generate_file_%d", data_size);
			int written_bytes = generate_file(&path, data_size);
			BSHG_setTracepoint_extended("generate_file_done");

			// Write the first bytes of the random data to the file
			if (written_bytes > 0) {
				benchi_create_reply_msg(reply, 0,
							"Successfully generated file with %d bytes",
							data_size);
				return E_SUCCESS;
			} else {
				benchi_create_reply_msg(
					reply, 1, "Error: Failed to generated file with %d bytes",
					data_size);
				return E_ERROR;
			}
		} else if (!strncmp(action.data, "remove", 4)) {
			BSHG_setTracepoint_extended("remove_file");
			int status = remove_file(&path);
			BSHG_setTracepoint_extended("remove_file_done");
			if (status >= 0) {
				benchi_create_reply_msg(reply, 0, "Successfully removed file");
			} else {
				benchi_create_reply_msg(reply, 1, "Error: failed to remove file %s",
							path.data);
				return E_ERROR;
			}
		} else if (!strncmp(action.data, "raw_write", 9) ||
			   !strncmp(action.data, "raw_clean_write", 15)) {
			const uint32_t data_offset = strtoll(offset.data, NULL, 10);

			if (jsmn_remove_quotes(&data)) {
				benchi_create_reply_msg(reply, 1, "Error: invalid data");
				return E_ERROR;
			}

			int status = 0;
			if (!strncmp(action.data, "raw_write", 9)) {
				BSHG_setTracepoint_extended("raw_write");
				status = benchi_flash_raw_write(&data, data_offset);
			} else {
				BSHG_setTracepoint_extended("raw_clean_write");
				status = benchi_flash_raw_clean_write(&data, data_offset);
			}
			BSHG_setTracepoint_extended("raw_write_dones");

			if (status > 0) {
				benchi_create_reply_msg(reply, 0,
							"Successfully generated raw flash data");
			} else {
				benchi_create_reply_msg(reply, 1,
							"Error: failed to generate raw flash data");
				return E_ERROR;
			}
		} else if (!strncmp(action.data, "raw_generate", 12) ||
			   !strncmp(action.data, "raw_clean_generate", 18)) {
			const uint32_t data_offset = strtoll(offset.data, NULL, 10);
			const uint32_t data_size = strtoll(size.data, NULL, 10);

			int status = 0;
			if (!strncmp(action.data, "raw_generate", 12)) {
				BSHG_setTracepoint_extended("raw_generate");
				status = benchi_flash_raw_generate(data_offset, data_size);
			} else {
				BSHG_setTracepoint_extended("raw_clean_generate");
				status = benchi_flash_raw_clean_generate(data_offset, data_size);
			}
			BSHG_setTracepoint_extended("raw_generate_done");

			if (status > 0) {
				benchi_create_reply_msg(reply, 0,
							"Successfully generated raw flash data");
			} else {
				benchi_create_reply_msg(reply, 1,
							"Error: failed to generate raw flash data");
				return E_ERROR;
			}
		} else if (!strncmp(action.data, "raw_erase_full", 14)) {
			BSHG_setTracepoint_extended("raw_erase_full");
			int status = benchi_flash_erase_full();
			BSHG_setTracepoint_extended("raw_erase_full_done");
			if (status == 0) {
				benchi_create_reply_msg(reply, 0,
							"Successfully fully erased flash");
			} else {
				benchi_create_reply_msg(reply, 1,
							"Error: failed to fully erase flash");
				return E_ERROR;
			}
		} else if (!strncmp(action.data, "raw_erase", 9)) {
			const uint32_t data_offset = strtoll(offset.data, NULL, 10);
			const uint32_t data_size = strtoll(size.data, NULL, 10);

			BSHG_setTracepoint_extended("raw_erase");
			int status = benchi_flash_erase(data_offset, data_size);
			BSHG_setTracepoint_extended("raw_erase_done");

			if (status == 0) {
				benchi_create_reply_msg(reply, 0, "Successfully erased flash");
			} else {
				benchi_create_reply_msg(reply, 1, "Error: failed to erase flash");
				return E_ERROR;
			}
		} else if (!strncmp(action.data, "raw_verify", 10)) {
			if (jsmn_remove_quotes(&data)) {
				benchi_create_reply_msg(reply, 1, "Error: invalid data");
				return E_ERROR;
			}

			const uint32_t data_offset = strtoll(offset.data, NULL, 10);
			const uint32_t data_size = data.length;
			const uint8_t *data_expected = data.data;

			BSHG_setTracepoint_extended("raw_verify");
			int status = benchi_flash_verify(data_offset, data_size, data_expected);
			BSHG_setTracepoint_extended("raw_verify_done");

			if (status == 0) {
				benchi_create_reply_msg(
					reply, 0, "Successfully verified %d bytes at offset %d",
					data_size, data_offset);
			} else {
				benchi_create_reply_msg(reply, 1,
							"Failed to verify %d bytes at offset %d",
							data_size, data_offset);
				return E_ERROR;
			}
		} else if (!strncmp(action.data, "raw_erase", 9)) {
			const uint32_t data_offset = strtoll(offset.data, NULL, 10);
			const uint32_t data_size = strtoll(size.data, NULL, 10);

			BSHG_setTracepoint_extended("raw_erase");
			int status = benchi_flash_erase(data_offset, data_size);
			BSHG_setTracepoint_extended("raw_erase_done");

			if (status == 0) {
				benchi_create_reply_msg(reply, 0, "Successfully erased flash");
			} else {
				benchi_create_reply_msg(reply, 1, "Error: failed to erase flash");
				return E_ERROR;
			}
		} else if (!strncmp(action.data, "raw_ifp_fixed", 13)) {
			if (jsmn_remove_quotes(&size) && jsmn_remove_quotes(&offset)) {
				benchi_create_reply_msg(reply, 1, "Error: invalid data");
				return E_ERROR;
			}

			int status = 0;

			BSHG_setTracepoint_extended("ifp");
			status = benchi_flash_raw_write_ifp_fixed(&size, &offset);

			if (status == 0) {
				BSHG_setTracepoint_extended("ifp_done");
				benchi_create_reply_msg(reply, 0, "Successfully ifp");
			} else {
				BSHG_setTracepoint_extended("ifp_error");
				benchi_create_reply_msg(reply, 1, "Error: failed to ifp");
				return E_ERROR;
			}
		} else {
			benchi_create_reply_msg(reply, 1, "Error: invalid action");
			return E_ERROR;
		}
	}
	return E_SUCCESS;
}
