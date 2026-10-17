/* SPDX-License-Identifier: GPL-2.0+ */
#ifdef H3C_NAND_ECC_HOST_TEST
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#else
#include <common.h>
#include <asm/errno.h>
#include <linux/kernel.h>
#endif
#include <h3c_nand_ecc.h>

static int h3c_nand_ecc_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* A reserved token is exactly "key" or begins with "key=". */
static int h3c_nand_ecc_reserved(const char *token, size_t length,
				 const char *key, size_t key_length)
{
	return length >= key_length &&
	       !memcmp(token, key, key_length) &&
	       (length == key_length || token[key_length] == '=');
}

static int h3c_nand_ecc_parse_value(const char *token, size_t length,
				    size_t key_length,
				    unsigned int *value)
{
	unsigned int parsed = 0;
	size_t i;

	/* Require key=decimal digits; empty, signed, hexadecimal, and suffixed
	 * values are rejected. This matches the decimal form already emitted by
	 * nx30_d_ecc_bootargs(), while avoiding partial numeric acceptance.
	 */
	if (length <= key_length + 1)
		return -EINVAL;
	for (i = key_length + 1; i < length; i++) {
		unsigned int digit;

		if (token[i] < '0' || token[i] > '9')
			return -EINVAL;
		digit = (unsigned int)(token[i] - '0');
		if (parsed > (UINT_MAX - digit) / 10U)
			return -EINVAL;
		parsed = parsed * 10U + digit;
	}
	*value = parsed;
	return 0;
}

static int h3c_nand_ecc_append(char *output, size_t capacity, size_t *used,
			      const char *text, size_t length)
{
	size_t separator = *used ? 1U : 0U;

	if (*used >= capacity || separator > capacity - *used - 1U ||
	    length >= capacity - *used - separator)
		return -ENOSPC;
	if (separator)
		output[(*used)++] = ' ';
	memcpy(output + *used, text, length);
	*used += length;
	output[*used] = '\0';
	return 0;
}

int h3c_nand_ecc_bootargs(const char *input, char *output, size_t capacity,
			  unsigned int physical_spare, unsigned int ecc_bits)
{
	static const char spare_key[] = "nandspare";
	static const char bits_key[] = "nandeccbits";
	const size_t spare_key_length = sizeof(spare_key) - 1U;
	const size_t bits_key_length = sizeof(bits_key) - 1U;
	const char *p;
	size_t used = 0;
	int ret;

	if (!input || !output || !capacity || input == output)
		return -EINVAL;
	output[0] = '\0';
	if (!((physical_spare == 64U && ecc_bits == 4U) ||
	      (physical_spare == 128U &&
	       (ecc_bits == 4U || ecc_bits == 8U))))
		return -EINVAL;

	for (p = input; *p;) {
		const char *start;
		size_t length;
		unsigned int value;
		int reserved;

		while (*p && h3c_nand_ecc_space(*p))
			p++;
		if (!*p)
			break;
		start = p;
		while (*p && !h3c_nand_ecc_space(*p))
			p++;
		length = (size_t)(p - start);

		reserved = h3c_nand_ecc_reserved(start, length, spare_key,
						spare_key_length);
		if (reserved) {
			ret = h3c_nand_ecc_parse_value(start, length,
						       spare_key_length, &value);
			if (ret || value != physical_spare)
				goto invalid;
			continue;
		}
		reserved = h3c_nand_ecc_reserved(start, length, bits_key,
						bits_key_length);
		if (reserved) {
			ret = h3c_nand_ecc_parse_value(start, length,
						       bits_key_length, &value);
			if (ret || value != ecc_bits)
				goto invalid;
			continue;
		}

		ret = h3c_nand_ecc_append(output, capacity, &used, start, length);
		if (ret)
			goto no_space;
	}

	{
		char canonical[48];
		int n = snprintf(canonical, sizeof(canonical),
				 "nandspare=%u nandeccbits=%u",
				 physical_spare, ecc_bits);

		if (n < 0 || (size_t)n >= sizeof(canonical))
			goto invalid;
		ret = h3c_nand_ecc_append(output, capacity, &used,
					  canonical, (size_t)n);
		if (ret)
			goto no_space;
	}
	return 0;

invalid:
	output[0] = '\0';
	return -EINVAL;
no_space:
	output[0] = '\0';
	return -ENOSPC;
}

unsigned int h3c_nand_ecc_unique_mode(int valid4, int valid8)
{
	if (!!valid4 == !!valid8)
		return 0;
	return valid4 ? 4U : 8U;
}
