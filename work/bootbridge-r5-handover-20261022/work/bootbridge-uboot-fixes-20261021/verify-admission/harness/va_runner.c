/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Independent admission-matrix runner. Includes the production identity
 * translation unit (compiled separately) and evaluates one case per line.
 * All expectations come from the matrix generator; this file only executes. */
#include "bridge_install_identity.h"
#include "va_matrix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENV_BYTES 0x80000u
#define ENV_CRC_BYTES 0x40000u

static uint32_t crc_env(const uint8_t *e)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 4; i < ENV_CRC_BYTES; i++) {
        c ^= e[i];
        for (unsigned j = 0; j < 8; j++) c = (c >> 1) ^ (0xedb88320u & -(int32_t)(c & 1));
    }
    return ~c;
}

/* Build the synthetic U-Boot ENV exactly the way the production reader expects:
 * 0x80000 bytes total, little-endian CRC over the first 0x40000. */
static uint8_t *make_env(const char *const *keys, int variant, size_t *out_size)
{
    uint8_t *e = malloc(ENV_BYTES);
    if (!e) return NULL;
    memset(e, 0xff, ENV_BYTES);
    memset(e + 4, 0, 2);
    size_t at = 4;
    for (size_t i = 0; keys && keys[i]; i++) {
        size_t n = strlen(keys[i]);
        if (at + n + 2 >= ENV_CRC_BYTES) { free(e); return NULL; }
        memcpy(e + at, keys[i], n);
        e[at + n] = 0;              /* terminate this key=value pair */
        at += n + 1;
    }
    e[at] = 0;                      /* double NUL terminates the env block */
    if (variant == 1) {             /* deliberately corrupt the CRC */
        uint32_t c = crc_env(e);
        for (unsigned i = 0; i < 4; i++) e[i] = (uint8_t)((c ^ 0x5a5a5a5au) >> (8 * i));
    } else {
        uint32_t c = crc_env(e);
        for (unsigned i = 0; i < 4; i++) e[i] = (uint8_t)(c >> (8 * i));
    }
    *out_size = (variant == 2) ? (ENV_BYTES - 1) : ENV_BYTES;   /* variant 2: wrong size */
    return e;
}

static const uint8_t *compat_for(int kind, size_t *len)
{
    static const uint8_t exact[] = "qcom,ipq5018-mp03.1-c2\0qcom,ipq5018";
    static const uint8_t near_mp031[] = "qcom,ipq5018-mp03.1\0qcom,ipq5018";
    static const uint8_t only_first[] = "qcom,ipq5018-mp03.1-c2\0";
    static const uint8_t only_generic[] = "qcom,ipq5018\0";
    static const uint8_t swapped[] = "qcom,ipq5018\0qcom,ipq5018-mp03.1-c2";
    static const uint8_t trailing[] = "qcom,ipq5018-mp03.1-c2\0qcom,ipq5018\0extra";
    static const uint8_t c2_only[] = "qcom,ipq5018-mp03.1-c2";
    static const uint8_t empty[] = "";
    switch (kind) {
    case VA_COMPAT_EXACT:      *len = sizeof(exact);           return exact;        /* 36 bytes */
    case VA_COMPAT_NEAR_MP031: *len = sizeof(near_mp031) - 1;   return near_mp031;
    case VA_COMPAT_ONLY_FIRST: *len = sizeof(only_first) - 1;   return only_first;
    case VA_COMPAT_ONLY_GENERIC:*len = sizeof(only_generic) - 1; return only_generic;
    case VA_COMPAT_SWAPPED:    *len = sizeof(swapped) - 1;      return swapped;
    case VA_COMPAT_TRAILING:   *len = sizeof(trailing) - 1;     return trailing;
    case VA_COMPAT_C2_NO_NUL:  *len = sizeof(c2_only) - 1;      return c2_only;
    case VA_COMPAT_EMPTY:      *len = 0;                        return empty;
    case VA_COMPAT_TRUNC_ONE:  *len = sizeof(exact) - 1;        return exact;        /* 35 of 36 */
    case VA_COMPAT_NULL:       *len = 0;                        return NULL;
    default:                   *len = 0;                        return NULL;
    }
}

static int same(const char *expect, bii_code actual, int is_runtime)
{
    const char *got = bii_code_name(actual);
    if (!strcmp(expect, "not-ok")) return actual != BII_OK;
    if (!strcmp(expect, "not-ok-runtime")) return !(is_runtime && actual == BII_OK);
    return !strcmp(expect, got);
}

int main(void)
{
    unsigned pass = 0, fail = 0;
    printf("{\"cases\":[\n");
    for (size_t i = 0; i < VA_CASE_COUNT; i++) {
        const va_case *c = &va_cases[i];
        size_t env_size = 0, compat_len = 0;
        uint8_t *env = make_env(c->env, c->env_variant, &env_size);
        const uint8_t *compat = compat_for(c->compat, &compat_len);
        if (!env) { fprintf(stderr, "env alloc failed for %s\n", c->id); return 2; }
        bii_result r; memset(&r, 0, sizeof(r));
        bii_code rt = bii_check_public_candidate_runtime(c->board, c->product, c->source,
            c->machid, c->candidate, env, env_size, c->cmdline, compat, compat_len, &r);
        bii_result r2; memset(&r2, 0, sizeof(r2));
        bii_code plain = bii_check_public_candidate(c->board, c->product, c->source,
            c->machid, c->candidate, env, env_size, c->cmdline, &r2);
        int ok = same(c->expect, rt, 1);
        if (ok) pass++; else fail++;
        printf("%s{\"id\":\"%s\",\"category\":\"%s\",\"expected\":\"%s\",\"actual\":\"%s\","
               "\"actual_plain\":\"%s\",\"pass\":%s,\"product_seen\":\"%s\",\"machid_seen\":\"%s\"}",
               i ? ",\n" : "", c->id, c->category, c->expect, bii_code_name(rt),
               bii_code_name(plain), ok ? "true" : "false", r.product, r.machid);
        free(env);
    }
    printf("\n],\"pass\":%u,\"fail\":%u,\"total\":%u}\n", pass, fail, pass + fail);
    return fail ? 1 : 0;
}
