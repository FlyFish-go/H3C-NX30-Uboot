/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FAILSAFE_ENV_TEXT_H
#define __FAILSAFE_ENV_TEXT_H

#include <errno.h>
#include <stddef.h>

struct failsafe_env_text_buffer {
	char *data;
	size_t size;
	size_t len;
};

static inline int failsafe_env_text_append(struct failsafe_env_text_buffer *text,
					   const char *key, const char *value)
{
	size_t key_len, value_len, need;

	if (!text || !key || !value)
		return -EINVAL;
	key_len = strlen(key);
	value_len = strlen(value);
	if (value_len > (size_t)-1 - key_len - 2)
		return -EOVERFLOW;
	need = key_len + value_len + 2;
	if (!text->data) {
		if (text->len > (size_t)-1 - need)
			return -EOVERFLOW;
		text->len += need;
		return 0;
	}
	if (text->len >= text->size || need > text->size - text->len - 1)
		return -E2BIG;
	memcpy(text->data + text->len, key, key_len);
	text->len += key_len;
	text->data[text->len++] = 61;
	memcpy(text->data + text->len, value, value_len);
	text->len += value_len;
	text->data[text->len++] = 10;
	text->data[text->len] = 0;
	return 0;
}

#endif
