#include "bridge_version.h"
#include <string.h>

static int hash_valid(const char *s)
{
    size_t i;
    if (!s) return 0;
    for (i = 0; i < 64; ++i)
        if (!s[i] || !((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return 0;
    return s[64] == 0;
}

static int scope_valid(const char *s)
{
    size_t i;
    if (!s || !s[0]) return 0;
    for (i = 0; i <= 127 && s[i]; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= '0' && s[i] <= '9') || s[i] == '-' || s[i] == '_' ||
              s[i] == '.' || s[i] == ':')) return 0;
    return i <= 127;
}

bv_result bv_evaluate(const bv_record *registry, size_t count,
                      const char *scope, const char *candidate_sha256,
                      uint64_t candidate_bytes, const char *installed_sha256,
                      uint64_t installed_bytes)
{
    bv_result result = { BV_REJECT_UNKNOWN, 0, 0 };
    const bv_record *candidate = NULL, *installed = NULL;
    size_t i, j;
    if (!registry || !count) return result;
    result.decision = BV_INVALID_REGISTRY;
    if (count > 128 || !scope_valid(scope) || !hash_valid(candidate_sha256) ||
        !hash_valid(installed_sha256) || !candidate_bytes || !installed_bytes)
        return result;
    for (i = 0; i < count; ++i) {
        const bv_record *r = &registry[i];
        if (!hash_valid(r->sha256) || !scope_valid(r->scope) || !r->bytes ||
            !((r->kind == BV_OEM_BASELINE && r->sequence == 0) ||
              (r->kind == BV_CUSTOM_RELEASE && r->sequence > 0)))
            return result;
        for (j = 0; j < i; ++j) {
            const bv_record *prior = &registry[j];
            if (!strcmp(r->sha256, prior->sha256) ||
                (r->kind == BV_CUSTOM_RELEASE && prior->kind == BV_CUSTOM_RELEASE &&
                 !strcmp(r->scope, prior->scope) && r->sequence == prior->sequence))
                return result;
        }
        if (!strcmp(r->sha256, candidate_sha256) && r->bytes == candidate_bytes)
            candidate = r;
        if (!strcmp(r->sha256, installed_sha256) && r->bytes == installed_bytes)
            installed = r;
    }
    result.decision = BV_REJECT_UNKNOWN;
    if (!candidate || !installed) return result;
    result.candidate_sequence = candidate->sequence;
    result.installed_sequence = installed->sequence;
    result.decision = BV_REJECT_INCOMPATIBLE;
    if (candidate->kind != BV_CUSTOM_RELEASE || strcmp(scope, candidate->scope) ||
        strcmp(scope, installed->scope)) return result;
    if (candidate == installed) result.decision = BV_SKIP_IDENTICAL;
    else if (installed->kind == BV_OEM_BASELINE) result.decision = BV_FIRST_INSTALL;
    else if (installed->sequence < candidate->sequence) result.decision = BV_UPGRADE;
    else result.decision = BV_REJECT_DOWNGRADE;
    return result;
}
