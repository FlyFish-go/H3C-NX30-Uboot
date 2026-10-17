/* SPDX-License-Identifier: GPL-2.0+ */
#ifdef NX30_D_BOOTARGS_HOST_TEST
#include <stdio.h>
#include <string.h>
#include <errno.h>
#else
#include <common.h>
#include <asm/errno.h>
#endif
#include <nx30_d_bootargs.h>

static int nx30_space(char c)
{
 return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int check_override(const char *token, size_t length,
                          const char *prefix, unsigned int expected)
{
 size_t n = strlen(prefix), i;
 unsigned int value = 0;
 if (length < n || memcmp(token, prefix, n)) return 0;
 if (length == n) return -EINVAL;
 for (i = n; i < length; i++) {
  if (token[i] < '0' || token[i] > '9' || value > 1000) return -EINVAL;
  value = value * 10 + token[i] - '0';
 }
 return value == expected ? 1 : -EINVAL;
}

int nx30_d_ecc_bootargs(const char *input, char *output, size_t capacity,
                      unsigned int physical_spare, unsigned int ecc_bits)
{
 const char *p;
 size_t used = 0;
 int ret, n;
 if (!input || !output || !capacity || input == output ||
     physical_spare != 128 || (ecc_bits != 4 && ecc_bits != 8)) return -EINVAL;
 output[0] = 0;
 for (p = input; *p;) {
  const char *start;
  size_t length;
  while (nx30_space(*p)) p++;
  if (!*p) break;
  start = p;
  while (*p && !nx30_space(*p)) p++;
  length = p - start;
  ret = check_override(start, length, "nandspare=", physical_spare);
  if (!ret) ret = check_override(start, length, "nandeccbits=", ecc_bits);
  if (ret < 0) return ret;
  if (ret) continue;
  if (used && used + 1 >= capacity) return -ENOSPC;
  if (used) output[used++] = ' ';
  if (length >= capacity - used) return -ENOSPC;
  memcpy(output + used, start, length); used += length; output[used] = 0;
 }
 n = snprintf(output + used, capacity - used, "%snandspare=%u nandeccbits=%u",
              used ? " " : "", physical_spare, ecc_bits);
 if (n < 0 || (size_t)n >= capacity - used) return -ENOSPC;
 return 0;
}
