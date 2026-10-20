#include "bb_catalog.h"
#include <assert.h>
#include <string.h>

static const bb_plan_op op = { BB_PLAN_COPY, 0, 1, NULL };
static bb_plan_recipe plans[2] = {
    { "out-b", 9, {1}, 9, {2}, &op, 1 },
    { "out-c", 9, {1}, 9, {3}, &op, 1 }
};
static const bb_board providers_b[] = { BB_BOARD_HW_B };
static const bb_board providers_c[] = { BB_BOARD_HW_C };
static const bb_board providers_bc[] = { BB_BOARD_HW_B, BB_BOARD_HW_C };
static const bb_board providers_abcd[] = { BB_BOARD_HW_A, BB_BOARD_HW_B, BB_BOARD_HW_C, BB_BOARD_HW_D };
static const bb_plan_op nx_op = { BB_PLAN_COPY, 0, 1, NULL };
static bb_plan_recipe nx_plan = { "nx-shared-source", 13, {13}, 13, {14}, &nx_op, 1 };
static bb_bundle_rule split[2] = {
    { "rt-b", BB_MODEL_RT3000, "RT3000", "R015", "test", 1, &plans[0], providers_b, 1 },
    { "rt-c", BB_MODEL_RT3000, "RT3000", "R015", "test", 1, &plans[1], providers_c, 1 }
};
static bb_bundle_rule merged = {
    "rt-bc", BB_MODEL_RT3000, "RT3000", "R015", "test", 1, &plans[0], providers_bc, 2
};
static bb_bundle_rule shared_nx_rx = {
    "nx-rx-shared-source", BB_MODEL_NX30, "NX30 / RX3000", "R012", "test", 1,
    &nx_plan, providers_abcd, 4
};

int main(void)
{
    unsigned char hash[32] = {1};
    const bb_bundle_rule *selected = NULL;
    bb_bundle_catalog c = { BB_BUNDLE_CATALOG_SCHEMA, "test", split, 2 };
    assert(bb_select_bundle(&c, 9, hash, &selected) == BB_SELECT_AMBIGUOUS_BUNDLE);
    assert(selected == NULL);
    c.bundles = &merged; c.bundle_count = 1;
    assert(bb_select_bundle(&c, 9, hash, &selected) == BB_SELECT_OK);
    assert(selected == &merged && selected->provider_count == 2);
    hash[0] = 13;
    c.bundles = &shared_nx_rx; c.bundle_count = 1;
    assert(bb_select_bundle(&c, 13, hash, &selected) == BB_SELECT_OK);
    assert(selected == &shared_nx_rx && selected->provider_count == 4);
    c.schema = "bootbridge-compiled-catalog-v2";
    assert(bb_select_bundle(&c, 13, hash, &selected) == BB_SELECT_INVALID_CATALOG);
    assert(selected == NULL);
    c.schema = BB_BUNDLE_CATALOG_SCHEMA; c.bundles = split; c.bundle_count = 2; hash[0] = 1;
    split[1].id = split[0].id;
    assert(bb_select_bundle(&c, 9, hash, &selected) == BB_SELECT_INVALID_CATALOG);
    return 0;
}
