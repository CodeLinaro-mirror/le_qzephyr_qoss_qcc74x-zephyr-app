/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef JSMN_EXTENDED_H
#define JSMN_EXTENDED_H

#define JSMN_STATIC
#define JSMN_PARENT_LINKS

#include <jsmn.h>
#include <string.h>
#include <stdint.h>

#define JSMN_TOKENS_MAX 50

typedef struct jsmn_json_t {
	const unsigned char *data;
	size_t length;
} jsmn_json_t;

typedef jsmn_json_t jsmn_property_t;

JSMN_API inline int jsmn_remove_quotes(jsmn_json_t *json)
{
	if (json->length < 2) {
		return -1;
	}
	if (json->data[0] != '"') {
		return -2;
	}
	if (json->data[json->length - 1] != '"') {
		return -3;
	}
	json->data += 1;
	json->length -= 2;
	return 0;
}

JSMN_API inline int jsmn_get_object_property_ptr(const jsmn_json_t *json,
						 const jsmn_property_t *property,
						 jsmn_json_t *result)
{
	int retval = -1;
	jsmn_parser parser;
	jsmntok_t tokens[JSMN_TOKENS_MAX]; // Adjust size depending on JSON complexity
	jsmn_init(&parser);

	int token_count = jsmn_parse(&parser, json->data, json->length, tokens, JSMN_TOKENS_MAX);
	if (token_count < 0) {
		return -2; // Parsing error
	}

	size_t token_len = 0;
	size_t substr_offset = 0;
	int substr_length = 0;

	// Search for the property key
	for (int i = 0; i < token_count - 1; i++) {
		if (tokens[i].parent != 0) // consider only top level keys
		{
			continue;
		}
		if (tokens[i].type != JSMN_STRING) // property must be of type string
		{
			continue;
		}

		token_len = tokens[i].end - tokens[i].start;
		if (token_len != property->length) {
			continue;
		}

		if (strncmp(":", json->data + tokens[i].end + 1,
			    1)) // token must be a property (followed by a :)
		{
			continue;
		}

		if (strncmp(json->data + tokens[i].start, property->data, property->length)) {
			continue;
		}

		// Success: Next token should be the property value
		const jsmntok_t *value_token = &tokens[i + 1];
		substr_length = value_token->end - value_token->start;
		substr_offset = value_token->start;
		if (value_token->type == JSMN_STRING) // workaround: do not cut the quotes around a
						      // string value by default
		{
			substr_offset -= 1;
			substr_length += 2;
		}

		result->length = substr_length;
		result->data = json->data + substr_offset;
		retval = 0;
		break;
	}

	return retval; // Property not found
}

JSMN_API int inline jsmn_get_array_items_ptrs(const char *json_str, size_t json_str_len,
					      jsmn_json_t *items)
{
	jsmn_parser parser;
	jsmntok_t tokens[200]; // Adjust size depending on JSON complexity
	jsmn_init(&parser);

	int token_count = jsmn_parse(&parser, json_str, json_str_len, tokens, 20);
	if (token_count < 0) {
		return -1; // Parsing error
	}

	uint16_t item_counter = 0;

	// Search for the property key
	for (int i = 0; i < token_count - 1; i++) {
		if (tokens[i].parent != 0) {
			continue;
		}

		items[item_counter].data = (char *)&json_str[tokens[i].start];
		items[item_counter].length = tokens[i].end - tokens[i].start;

		item_counter++;
	}

	return 0;
}
#endif // JSMN_EXTENDED_H