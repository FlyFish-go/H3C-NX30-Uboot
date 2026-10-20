#ifndef BB_CATALOG_H
#define BB_CATALOG_H

#include "../src/bb_export.h"
#include "../src/bb_export_plan.h"

#ifndef BB_ENGINE_REVISION
#define BB_ENGINE_REVISION 2U
#endif
#ifndef BB_RELEASE_SET_ID
#define BB_RELEASE_SET_ID "development-unreleased"
#endif

typedef enum {
    BB_MODEL_RT3000 = 1, BB_MODEL_RW3000, BB_MODEL_RC3000,
    BB_MODEL_RX3000, BB_MODEL_NX30
} bb_model;

/* Board is an explicit release target. UNSPECIFIED exists only for legacy
 * synthetic catalogs and must never be inferred by the production GUI. */
typedef enum {
    BB_BOARD_UNSPECIFIED = 0,
    BB_BOARD_HW_A = 1, BB_BOARD_HW_B, BB_BOARD_HW_C, BB_BOARD_HW_D
} bb_board;

typedef struct {
    bb_model model;
    const char *official_version;
    const char *release_set;
    unsigned int minimum_engine_revision;
    bb_recipe recipe;
    const bb_plan_recipe *plan;
    /* Appended so legacy source initializers default to UNSPECIFIED. */
    bb_board board;
} bb_rule;

typedef struct {
    const char *release_set;
    const bb_rule *rules;
    size_t rule_count;
} bb_catalog;

/* Offline bundle catalog: one output artifact per exact official input.
 * Provider classes are private metadata for the bridge's own hardware
 * dispatcher; the desktop selector never asks the user to choose one. */
#define BB_BUNDLE_CATALOG_SCHEMA "bootbridge-offline-bundle-catalog-v1"
typedef struct {
    const char *id;
    bb_model model;
    const char *display_models; /* Audited logical coverage labels; UI only. */
    const char *official_version;
    const char *release_set;
    unsigned int minimum_engine_revision;
    const bb_plan_recipe *plan;
    const bb_board *provider_classes;
    size_t provider_count;
} bb_bundle_rule;

typedef struct {
    const char *schema;
    const char *release_set;
    const bb_bundle_rule *bundles;
    size_t bundle_count;
} bb_bundle_catalog;

/* Display-only model guide. It is never an input to bundle selection or
 * export; the file's size and SHA-256 remain the sole production selector. */
typedef struct {
    const wchar_t *id;
    bb_model model;
    const wchar_t *primary;
    const wchar_t *alias;
    const wchar_t *version;
    uint64_t bytes;
    const wchar_t *sha256;
    const wchar_t *url;
} bb_guide_model;

typedef enum {
    BB_SELECT_OK = 0, BB_SELECT_INVALID_MODEL, BB_SELECT_UNSUPPORTED_OFFICIAL,
    BB_SELECT_MODEL_MISMATCH, BB_SELECT_UPDATE_REQUIRED, BB_SELECT_INVALID_CATALOG,
    BB_SELECT_INVALID_BOARD, BB_SELECT_BOARD_REQUIRED, BB_SELECT_BOARD_MISMATCH,
    BB_SELECT_AMBIGUOUS_BUNDLE
} bb_select_status;

/* Only release-engineering compiled catalogs are trusted. Fingerprints supplied
 * by the GUI are for selection; bb_export_bin MUST verify actual file bytes.
 * On failure *selected is NULL. No fallback to another model/version occurs.
 */
bb_select_status bb_select_rule(const bb_catalog *catalog, bb_model model,
                                uint64_t input_size,
                                const unsigned char input_sha256[32],
                                const bb_rule **selected);

/* Production selection requires both the chosen product name and an explicit
 * HW class. Rules with no board binding are deliberately not fallback matches.
 */
bb_select_status bb_select_rule_for_board(const bb_catalog *catalog,
                                          bb_model model, bb_board board,
                                          uint64_t input_size,
                                          const unsigned char input_sha256[32],
                                          const bb_rule **selected);

/* Match a bundle from the exact input bytes alone. Exactly one catalog entry
 * must match; matching legacy per-board rules are ambiguous and never choose
 * a hardware class implicitly. */
bb_select_status bb_select_bundle(const bb_bundle_catalog *catalog,
                                  uint64_t input_size,
                                  const unsigned char input_sha256[32],
                                  const bb_bundle_rule **selected);

const char *bb_model_name(bb_model model);
const char *bb_board_name(bb_board board);
const bb_catalog *bb_production_catalog(void);

#endif
