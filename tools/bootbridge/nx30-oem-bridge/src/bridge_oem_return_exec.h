#ifndef BRIDGE_OEM_RETURN_EXEC_H
#define BRIDGE_OEM_RETURN_EXEC_H
/* Execute the independently built return service only after verifying its
 * immutable compile-time size and digest. Diagnostics are informational. */
int bi_oem_return_exec_verified(int rootfd, const char *reason_code,
                                const char *reason_text,
                                const char *candidate_source_revision_sha256);
int bi_oem_return_exec_verified_detail(int rootfd, const char *reason_code,
                                const char *reason_text,
                                const char *candidate_source_revision_sha256,
                                const char *execution_detail);
#endif
