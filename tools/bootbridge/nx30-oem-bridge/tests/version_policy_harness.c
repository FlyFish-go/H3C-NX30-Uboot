#include "../src/bridge_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static int checks;
static const char *sha[5] = {
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
    "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
    "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
};
int main(void)
{
    bv_record r[4] = {
        { NULL, "d-ecc4-r012", 0x140000, 0, BV_OEM_BASELINE },
        { NULL, "d-ecc4-r012", 0x140000, 1, BV_CUSTOM_RELEASE },
        { NULL, "d-ecc4-r012", 0x140000, 2, BV_CUSTOM_RELEASE },
        { NULL, "d-ecc8-other", 0x140000, 1, BV_CUSTOM_RELEASE }
    };
    bv_result result;
    for (int i = 0; i < 4; ++i) r[i].sha256 = sha[i];
#define EVAL(candidate,installed) bv_evaluate(r,4,"d-ecc4-r012",sha[candidate],0x140000,sha[installed],0x140000)
    result = EVAL(2,0); CHECK(result.decision == BV_FIRST_INSTALL);
    CHECK(result.candidate_sequence == 2 && result.installed_sequence == 0);
    CHECK(EVAL(2,1).decision == BV_UPGRADE);
    CHECK(EVAL(2,2).decision == BV_SKIP_IDENTICAL);
    CHECK(EVAL(1,2).decision == BV_REJECT_DOWNGRADE);
    CHECK(EVAL(2,3).decision == BV_REJECT_INCOMPATIBLE);
    CHECK(EVAL(3,2).decision == BV_REJECT_INCOMPATIBLE);
    CHECK(EVAL(0,2).decision == BV_REJECT_INCOMPATIBLE);
    CHECK(EVAL(2,4).decision == BV_REJECT_UNKNOWN);
    CHECK(EVAL(4,1).decision == BV_REJECT_UNKNOWN);
    CHECK(bv_evaluate(NULL,0,"d-ecc4-r012",sha[2],0x140000,sha[1],0x140000).decision == BV_REJECT_UNKNOWN);
    CHECK(bv_evaluate(r,4,"d-ecc4-r012",sha[2],4,sha[1],0x140000).decision == BV_REJECT_UNKNOWN);
    CHECK(bv_evaluate(r,4,"d-ecc4-r012",sha[2],0x140000,sha[1],4).decision == BV_REJECT_UNKNOWN);
    CHECK(bv_evaluate(r,4,NULL,sha[2],0x140000,sha[1],0x140000).decision == BV_INVALID_REGISTRY);
    CHECK(bv_evaluate(r,4,"d-ecc4-r012","version-9999",0x140000,sha[1],0x140000).decision == BV_INVALID_REGISTRY);
    r[1].sequence = 0; CHECK(EVAL(2,0).decision == BV_INVALID_REGISTRY); r[1].sequence = 1;
    r[0].sequence = 1; CHECK(EVAL(2,0).decision == BV_INVALID_REGISTRY); r[0].sequence = 0;
    r[1].sha256 = sha[2]; CHECK(EVAL(2,0).decision == BV_INVALID_REGISTRY); r[1].sha256 = sha[1];
    r[1].sequence = 2; CHECK(EVAL(2,0).decision == BV_INVALID_REGISTRY); r[1].sequence = 1;
    r[1].scope = "untrusted text"; CHECK(EVAL(2,0).decision == BV_INVALID_REGISTRY);
    printf("PASS %d trusted version policy assertions (synthetic registry only)\n", checks);
    return 0;
}
