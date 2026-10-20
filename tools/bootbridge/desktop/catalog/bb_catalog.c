#include "bb_catalog.h"
#include <string.h>
#ifdef BB_RELEASE_CATALOG_HEADER
#include BB_RELEASE_CATALOG_HEADER
#endif

const char *bb_model_name(bb_model model)
{
    switch (model) {
    case BB_MODEL_RT3000: return "RT3000";
    case BB_MODEL_RW3000: return "RW3000";
    case BB_MODEL_RC3000: return "RC3000";
    case BB_MODEL_RX3000: return "RX3000";
    case BB_MODEL_NX30: return "NX30";
    default: return NULL;
    }
}

const char *bb_board_name(bb_board board)
{
    switch (board) {
    case BB_BOARD_HW_A: return "HW-A";
    case BB_BOARD_HW_B: return "HW-B";
    case BB_BOARD_HW_C: return "HW-C";
    case BB_BOARD_HW_D: return "HW-D";
    default: return NULL;
    }
}

static int nonempty(const char *s) { return s && s[0]; }

static const char *rule_id(const bb_rule *r) { return r->plan ? r->plan->id : r->recipe.id; }
static uint64_t rule_input_size(const bb_rule *r) { return r->plan ? r->plan->input_size : r->recipe.input_size; }
static const unsigned char *rule_input_hash(const bb_rule *r) { return r->plan ? r->plan->input_sha256 : r->recipe.input_sha256; }
static uint64_t rule_output_size(const bb_rule *r) { return r->plan ? r->plan->output_size : r->recipe.output_size; }

static bb_select_status select_rule(const bb_catalog *catalog, bb_model model,
                                    bb_board board, int board_required,
                                    uint64_t input_size,
                                    const unsigned char input_sha256[32],
                                    const bb_rule **selected)
{
    const bb_rule *match = NULL;
    int another_model = 0, another_board = 0, unbound_match = 0;
    size_t i, j;

    if (!selected) return BB_SELECT_INVALID_CATALOG;
    *selected = NULL;
    if (!bb_model_name(model)) return BB_SELECT_INVALID_MODEL;
    if (board_required && !bb_board_name(board)) return BB_SELECT_INVALID_BOARD;
    if (!catalog || !input_sha256 || !nonempty(catalog->release_set) ||
        (catalog->rule_count && !catalog->rules))
        return BB_SELECT_INVALID_CATALOG;

    /* Check the entire catalog before picking a rule. Ambiguous identities or
     * mixed release resources must never turn into a first-entry-wins rule. */
    for (i = 0; i < catalog->rule_count; ++i) {
        const bb_rule *r = &catalog->rules[i];
        if (!bb_model_name(r->model) || !nonempty(r->official_version) ||
            !nonempty(r->release_set) ||
            strcmp(r->release_set, catalog->release_set) ||
            (r->board != BB_BOARD_UNSPECIFIED && !bb_board_name(r->board)) ||
            !r->minimum_engine_revision || !nonempty(rule_id(r)) ||
            !rule_input_size(r) || !rule_output_size(r) ||
            (r->plan && (!r->plan->operations || !r->plan->operation_count)))
            return BB_SELECT_INVALID_CATALOG;
        for (j = 0; j < i; ++j) {
            const bb_rule *other = &catalog->rules[j];
            if (!strcmp(rule_id(other), rule_id(r)) ||
                (other->model == r->model && other->board == r->board &&
                 rule_input_size(other) == rule_input_size(r) &&
                 !memcmp(rule_input_hash(other), rule_input_hash(r), 32)))
                return BB_SELECT_INVALID_CATALOG;
        }
        if (rule_input_size(r) == input_size &&
            !memcmp(rule_input_hash(r), input_sha256, 32)) {
            if (r->model != model) {
                another_model = 1;
            } else if (board_required) {
                if (r->board == board) match = r;
                else if (r->board == BB_BOARD_UNSPECIFIED) unbound_match = 1;
                else another_board = 1;
            } else if (r->board == BB_BOARD_UNSPECIFIED) {
                match = r;
            } else {
                /* The legacy API has no board argument. Never select a
                 * board-specific rule through it, even if a legacy rule with
                 * the same fingerprint is also present. */
                unbound_match = 1;
            }
        }
    }
    if (!match) {
        if (unbound_match) return BB_SELECT_BOARD_REQUIRED;
        if (another_board) return BB_SELECT_BOARD_MISMATCH;
        return another_model ? BB_SELECT_MODEL_MISMATCH : BB_SELECT_UNSUPPORTED_OFFICIAL;
    }
    if (!board_required && unbound_match) return BB_SELECT_BOARD_REQUIRED;
    if (match->minimum_engine_revision > BB_ENGINE_REVISION)
        return BB_SELECT_UPDATE_REQUIRED;
    *selected = match;
    return BB_SELECT_OK;
}

bb_select_status bb_select_rule(const bb_catalog *catalog, bb_model model,
                                uint64_t input_size,
                                const unsigned char input_sha256[32],
                                const bb_rule **selected)
{
    return select_rule(catalog, model, BB_BOARD_UNSPECIFIED, 0, input_size,
                       input_sha256, selected);
}

bb_select_status bb_select_rule_for_board(const bb_catalog *catalog,
                                          bb_model model, bb_board board,
                                          uint64_t input_size,
                                          const unsigned char input_sha256[32],
                                          const bb_rule **selected)
{
    return select_rule(catalog, model, board, 1, input_size, input_sha256,
                       selected);
}

bb_select_status bb_select_bundle(const bb_bundle_catalog *catalog,
                                  uint64_t input_size,
                                  const unsigned char input_sha256[32],
                                  const bb_bundle_rule **selected)
{
    const bb_bundle_rule *match = NULL;
    size_t matches = 0, i, j, k;
    if (!selected) return BB_SELECT_INVALID_CATALOG;
    *selected = NULL;
    if (!catalog || !input_sha256 || !nonempty(catalog->schema) ||
        strcmp(catalog->schema, BB_BUNDLE_CATALOG_SCHEMA) ||
        !nonempty(catalog->release_set) ||
        (catalog->bundle_count && !catalog->bundles))
        return BB_SELECT_INVALID_CATALOG;

    for (i = 0; i < catalog->bundle_count; ++i) {
        const bb_bundle_rule *b = &catalog->bundles[i];
        if (!nonempty(b->id) || !bb_model_name(b->model) ||
            !nonempty(b->display_models) ||
            !nonempty(b->official_version) || !nonempty(b->release_set) ||
            strcmp(b->release_set, catalog->release_set) ||
            !b->minimum_engine_revision || !b->plan ||
            !nonempty(b->plan->id) || !b->plan->input_size ||
            !b->plan->output_size || !b->plan->operations ||
            !b->plan->operation_count || !b->provider_classes ||
            !b->provider_count || b->provider_count > 4)
            return BB_SELECT_INVALID_CATALOG;
        for (j = 0; j < i; ++j) {
            if (!strcmp(catalog->bundles[j].id, b->id))
                return BB_SELECT_INVALID_CATALOG;
        }
        for (j = 0; j < b->provider_count; ++j) {
            if (!bb_board_name(b->provider_classes[j]))
                return BB_SELECT_INVALID_CATALOG;
            for (k = 0; k < j; ++k) {
                if (b->provider_classes[k] == b->provider_classes[j])
                    return BB_SELECT_INVALID_CATALOG;
            }
        }
        if (b->plan->input_size == input_size &&
            !memcmp(b->plan->input_sha256, input_sha256, 32)) {
            match = b;
            ++matches;
        }
    }
    if (matches > 1) return BB_SELECT_AMBIGUOUS_BUNDLE;
    if (!match) return BB_SELECT_UNSUPPORTED_OFFICIAL;
    if (match->minimum_engine_revision > BB_ENGINE_REVISION)
        return BB_SELECT_UPDATE_REQUIRED;
    *selected = match;
    return BB_SELECT_OK;
}

const bb_catalog *bb_production_catalog(void)
{
#ifdef BB_RELEASE_CATALOG_HEADER
    return &bb_generated_catalog;
#else
    /* No sample identity, lab candidate, synthetic rule or unqualified OEM
     * adaptation is a released conversion rule. Unknown input must refuse. */
    static const bb_catalog catalog = { BB_RELEASE_SET_ID, NULL, 0 };
    return &catalog;
#endif
}
