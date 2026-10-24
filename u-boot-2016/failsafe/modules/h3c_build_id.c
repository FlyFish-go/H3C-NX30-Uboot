// SPDX-License-Identifier: GPL-2.0+
/* Build identity: board, NAND profile, source mask and ECC of this U-Boot.
 * The rescue page accepts a U-Boot candidate only when it carries exactly one
 * identity string equal to the running build's, so a build for another board
 * or another NAND profile is refused before anything is erased.
 */
#ifdef H3C_BUILD_ID_HOST_TEST
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
#else
#include <common.h>
#include <asm/errno.h>
#include <h3c_build_id.h>
#endif

#define H3C_ID_STR2(x) #x
#define H3C_ID_STR(x) H3C_ID_STR2(x)

#if defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)
#define H3C_ID_BOARD "A"
#elif defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
#define H3C_ID_BOARD "C"
#elif defined(CONFIG_TARGET_IPQ5018_H3C_HW_D)
#define H3C_ID_BOARD "D"
#elif defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)
#define H3C_ID_BOARD "B"
#else
#define H3C_ID_BOARD "unknown"
#endif

#ifdef CONFIG_H3C_NAND_PROFILE_ID
#define H3C_ID_PROFILE CONFIG_H3C_NAND_PROFILE_ID
#else
#define H3C_ID_PROFILE ""
#endif
#ifdef CONFIG_H3C_NAND_SOURCE_MASK
#define H3C_ID_MASK H3C_ID_STR(CONFIG_H3C_NAND_SOURCE_MASK)
#else
#define H3C_ID_MASK "none"
#endif
#ifdef CONFIG_H3C_NAND_LOAD_ECC_BITS
#define H3C_ID_ECC H3C_ID_STR(CONFIG_H3C_NAND_LOAD_ECC_BITS)
#else
#define H3C_ID_ECC "none"
#endif

static const char h3c_build_id_string[] __attribute__((used)) =
	"H3C-UBOOT-ID:v1;board=" H3C_ID_BOARD ";profile=" H3C_ID_PROFILE
	";mask=" H3C_ID_MASK ";ecc=" H3C_ID_ECC ";";

/* The search key, each byte + 1, so this file is not itself a match. */
static const volatile char h3c_build_id_key[] = "I4D.VCPPU.JE;w2<";

const char *h3c_build_id(void)
{
	return h3c_build_id_string;
}

int h3c_build_id_check(const u8 *image, size_t size)
{
	char key[sizeof(h3c_build_id_key)];
	size_t i, key_len = sizeof(key) - 1;
	size_t own_len = strlen(h3c_build_id_string);
	unsigned int found = 0;
	int match = 0;

	if (!image)
		return -EINVAL;
	for (i = 0; i < key_len; i++)
		key[i] = h3c_build_id_key[i] - 1;
	key[key_len] = '\0';
	for (i = 0; i + key_len <= size; i++) {
		if (memcmp(image + i, key, key_len))
			continue;
		found++;
		/* Compare the terminating NUL too: a longer identity is different. */
		match = size - i > own_len &&
			!memcmp(image + i, h3c_build_id_string, own_len + 1);
	}
	if (!found)
		return -ENOENT;
	if (found != 1)
		return -EEXIST;
	return match ? 0 : -EXDEV;
}
