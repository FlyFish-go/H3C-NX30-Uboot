#ifndef BOOTBRIDGE_MTD_OBSERVER_H
#define BOOTBRIDGE_MTD_OBSERVER_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMO_APPSBL_BYTES UINT64_C(0x140000)
#define BMO_PAGE_BYTES 2048U
#define BMO_SHA256_HEX_BYTES 65U
#define BMO_REASON_BYTES 160U

enum bmo_result_code {
    BMO_RESULT_OK = 0,
    BMO_RESULT_NON_CLEAN = 1,
    BMO_RESULT_REJECTED = -1
};

enum bmo_ecc_stats_state {
    BMO_ECC_STATS_NOT_READ = 0,
    BMO_ECC_STATS_SUPPORTED = 1,
    BMO_ECC_STATS_UNSUPPORTED = 2
};

/* This is an observation report, never an installation authorization. */
struct bmo_partition_report {
    int observation_complete;
    int binding_valid;
    int clean;
    enum bmo_ecc_stats_state ecc_stats_state;
    uint64_t expected_bytes;
    uint64_t covered_bytes;
    uint32_t expected_page_bytes;
    uint32_t expected_erase_bytes;
    uint32_t observed_oob_bytes;
    uint32_t pages_expected;
    uint32_t pages_read;
    uint32_t eraseblocks_expected;
    uint32_t eraseblocks_checked;
    uint32_t badblocks;
    uint32_t ecc_stats_samples;
    uint32_t ecc_corrected_before;
    uint32_t ecc_corrected_after;
    uint32_t ecc_uncorrected_before;
    uint32_t ecc_uncorrected_after;
    char sha256[BMO_SHA256_HEX_BYTES];
    char reason[BMO_REASON_BYTES];
};

struct bmo_appsbl_pair_report {
    int observation_complete;
    int binding_valid;
    int clean;
    struct bmo_partition_report appsbl[2];
    char reason[BMO_REASON_BYTES];
};

/*
 * Observe one already-open MTD character device without taking fd ownership.
 * The caller must provide the expected st_rdev and complete partition geometry.
 * Only 2048-byte NAND pages are accepted. The descriptor must be O_RDONLY.
 * On every return, fd remains open and owned by the caller.
 */
int bmo_observe_partition(int fd, dev_t expected_dev,
                          uint64_t expected_size_bytes,
                          uint32_t expected_page_bytes,
                          uint32_t expected_erase_bytes,
                          struct bmo_partition_report *report);

/* Observe two full APPSBL extents (640 pages each) through caller-owned fds. */
int bmo_observe_appsbl_pair(int fd0, dev_t expected_dev0,
                            int fd1, dev_t expected_dev1,
                            uint32_t expected_page_bytes,
                            uint32_t expected_erase_bytes,
                            struct bmo_appsbl_pair_report *report);

#ifdef __cplusplus
}
#endif
#endif
