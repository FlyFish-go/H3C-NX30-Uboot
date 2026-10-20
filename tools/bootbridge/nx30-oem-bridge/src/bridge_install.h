#ifndef NX30_BRIDGE_INSTALL_H
#define NX30_BRIDGE_INSTALL_H

#include <stddef.h>
#include <stdint.h>

/* The fixture_root argument is honored only by BRIDGE_INSTALL_TEST builds. */
void bi_init(const char *fixture_root);
int bi_status_json(char *out, size_t cap);
int bi_start(const char *token, char *error, size_t error_cap);
int bi_backup_path(char *out, size_t cap);
/* User-confirmed return: BC-only verified transaction, then one restricted
 * reboot. Never installs APPSBL; no automatic retry on any failure. */
int bi_oem_return_start(const char *token, char *error, size_t error_cap);
/* Separate manual reboot after installation; independently rechecks OEM,
 * exact BC postimage and registered candidate boot policy before restarting. */
int bi_oem_reboot_start(const char *token, char *error, size_t error_cap);
/* Deprecated generic endpoint: always refused. */
int bi_reboot_start(const char *token, char *error, size_t error_cap);
void bi_reap(void);
/* Dispatcher diagnostics are informational and never affect capability gates. */
void bi_set_dispatch_diagnostic(const char *code, const char *reason,
                                const char *candidate_source_revision_sha256);
void bi_set_inspection_mode(int enabled);
int bi_dispatch_inspect_json(char *out, size_t cap);
int bi_set_dispatch_execution_detail(const char *detail);
#if defined(BRIDGE_PAYLOAD_PATCH_TEST)
int bi_test_reconstruct_payload(uint8_t *image, size_t image_bytes);
#endif

#endif
