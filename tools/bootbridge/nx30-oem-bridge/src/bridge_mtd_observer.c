#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#include "bridge_mtd_observer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mtd/mtd-user.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bridge_sha256.h"

static void set_reason(struct bmo_partition_report *r, const char *reason)
{
    if (r == NULL)
        return;
    (void)snprintf(r->reason, sizeof(r->reason), "%s", reason);
}

static int reject(struct bmo_partition_report *r, const char *reason)
{
    set_reason(r, reason);
    return BMO_RESULT_REJECTED;
}

static bool stats_unsupported_errno(int e)
{
    return e == ENOTTY || e == EOPNOTSUPP || e == ENOSYS;
}

static int get_stats(int fd, struct mtd_ecc_stats *stats)
{
    if (ioctl(fd, ECCGETSTATS, stats) == 0)
        return 0;
    if (stats_unsupported_errno(errno))
        return 1;
    return -1;
}

static int same_info(const struct mtd_info_user *a,
                     const struct mtd_info_user *b)
{
    return a->type == b->type && a->flags == b->flags &&
           a->size == b->size && a->erasesize == b->erasesize &&
           a->writesize == b->writesize && a->oobsize == b->oobsize &&
           a->padding == b->padding;
}

static int query_info(int fd, struct mtd_info_user *info)
{
    memset(info, 0, sizeof(*info));
    return ioctl(fd, MEMGETINFO, info);
}

static int check_descriptor(int fd, dev_t expected_dev,
                            struct bmo_partition_report *r,
                            struct stat *identity)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0)
        return reject(r, "fcntl(F_GETFL) failed");
    if ((flags & O_ACCMODE) != O_RDONLY)
        return reject(r, "MTD descriptor is not read-only");
    if (fstat(fd, identity) != 0)
        return reject(r, "fstat failed");
    if (!S_ISCHR(identity->st_mode))
        return reject(r, "descriptor is not a character device");
    if (identity->st_rdev != expected_dev)
        return reject(r, "character device st_rdev differs from expected dev_t");
    return 0;
}

static int stats_monotonic(const struct mtd_ecc_stats *before,
                           const struct mtd_ecc_stats *after,
                           struct bmo_partition_report *r)
{
    if (after->corrected < before->corrected ||
        after->failed < before->failed)
        return reject(r, "ECCGETSTATS counter regressed");
    if (after->failed != before->failed)
        return reject(r, "uncorrectable ECC counter increased during observation");
    return 0;
}

int bmo_observe_partition(int fd, dev_t expected_dev,
                          uint64_t expected_size_bytes,
                          uint32_t expected_page_bytes,
                          uint32_t expected_erase_bytes,
                          struct bmo_partition_report *report)
{
    struct stat before_stat, after_stat;
    struct mtd_info_user info_before, info_after;
    struct mtd_ecc_stats stats_before, stats_after, prior_stats;
    enum bmo_ecc_stats_state stats_state = BMO_ECC_STATS_NOT_READ;
    bi_sha256_ctx sha;
    uint8_t page[BMO_PAGE_BYTES], digest[32];
    uint64_t offset;
    int stats_rc, saw_stats_unsupported = 0;

    if (report == NULL)
        return BMO_RESULT_REJECTED;
    memset(report, 0, sizeof(*report));
    report->expected_bytes = expected_size_bytes;
    report->expected_page_bytes = expected_page_bytes;
    report->expected_erase_bytes = expected_erase_bytes;

    if (expected_page_bytes != BMO_PAGE_BYTES)
        return reject(report, "only 2048-byte NAND pages are supported");
    if (!expected_size_bytes || expected_size_bytes > UINT32_MAX ||
        expected_size_bytes % expected_page_bytes != 0 ||
        !expected_erase_bytes || expected_size_bytes % expected_erase_bytes != 0 ||
        expected_erase_bytes % expected_page_bytes != 0)
        return reject(report, "expected size/page/erase geometry is invalid");
    report->pages_expected = (uint32_t)(expected_size_bytes / expected_page_bytes);
    report->eraseblocks_expected = (uint32_t)(expected_size_bytes / expected_erase_bytes);

    if (check_descriptor(fd, expected_dev, report, &before_stat) != 0)
        return BMO_RESULT_REJECTED;
    if (query_info(fd, &info_before) != 0)
        return reject(report, "MEMGETINFO failed");
    report->observed_oob_bytes = info_before.oobsize;
    if (info_before.type != MTD_NANDFLASH)
        return reject(report, "MTD device is not NAND flash");
    if ((uint64_t)info_before.size != expected_size_bytes ||
        info_before.writesize != expected_page_bytes ||
        info_before.erasesize != expected_erase_bytes)
        return reject(report, "MEMGETINFO geometry differs from expected partition");

    for (uint64_t block = 0; block < expected_size_bytes;
         block += expected_erase_bytes) {
        int64_t badblock_offset = (int64_t)block;
        int is_bad = ioctl(fd, MEMGETBADBLOCK, &badblock_offset);
        if (is_bad < 0)
            return reject(report, "MEMGETBADBLOCK failed");
        report->eraseblocks_checked++;
        if (is_bad > 0) {
            report->badblocks++;
            return reject(report, "bad eraseblock found in observed partition");
        }
    }

    stats_rc = get_stats(fd, &stats_before);
    if (stats_rc < 0)
        return reject(report, "initial ECCGETSTATS failed");
    if (stats_rc > 0) {
        saw_stats_unsupported = 1;
        stats_state = BMO_ECC_STATS_UNSUPPORTED;
    } else {
        stats_state = BMO_ECC_STATS_SUPPORTED;
        report->ecc_stats_samples++;
        report->ecc_corrected_before = stats_before.corrected;
        report->ecc_corrected_after = stats_before.corrected;
        report->ecc_uncorrected_before = stats_before.failed;
        report->ecc_uncorrected_after = stats_before.failed;
        prior_stats = stats_before;
    }
    report->ecc_stats_state = stats_state;
    if (stats_state == BMO_ECC_STATS_SUPPORTED) {
        if (stats_before.failed != 0)
            return reject(report, "uncorrectable ECC counter was already nonzero");
    }

    bi_sha256_init(&sha);
    for (offset = 0; offset < expected_size_bytes; offset += expected_page_bytes) {
        int before_rc = get_stats(fd, &stats_before);
        if (before_rc < 0)
            return reject(report, "ECCGETSTATS failed before page read");
        if (before_rc > 0) {
            if (stats_state == BMO_ECC_STATS_SUPPORTED)
                return reject(report, "ECCGETSTATS support changed during observation");
            saw_stats_unsupported = 1;
        } else {
            if (stats_state == BMO_ECC_STATS_UNSUPPORTED)
                return reject(report, "ECCGETSTATS support changed during observation");
            stats_state = BMO_ECC_STATS_SUPPORTED;
            report->ecc_stats_state = stats_state;
            report->ecc_stats_samples++;
            report->ecc_corrected_after = stats_before.corrected;
            report->ecc_uncorrected_after = stats_before.failed;
            if (stats_monotonic(&prior_stats, &stats_before, report) != 0)
                return BMO_RESULT_REJECTED;
            prior_stats = stats_before;
        }

        ssize_t got;
        do {
            got = pread(fd, page, expected_page_bytes, (off_t)offset);
        } while (got < 0 && errno == EINTR);
        if (got < 0)
            return reject(report, "pread failed during page read");
        if ((size_t)got != expected_page_bytes)
            return reject(report, "short page read");

        int after_rc = get_stats(fd, &stats_after);
        if (after_rc < 0)
            return reject(report, "ECCGETSTATS failed after page read");
        if (after_rc > 0) {
            if (stats_state == BMO_ECC_STATS_SUPPORTED)
                return reject(report, "ECCGETSTATS support changed during observation");
            saw_stats_unsupported = 1;
        } else {
            if (stats_state == BMO_ECC_STATS_UNSUPPORTED)
                return reject(report, "ECCGETSTATS support changed during observation");
            stats_state = BMO_ECC_STATS_SUPPORTED;
            report->ecc_stats_state = stats_state;
            report->ecc_stats_samples++;
            report->ecc_corrected_after = stats_after.corrected;
            report->ecc_uncorrected_after = stats_after.failed;
            if (stats_monotonic(&stats_before, &stats_after, report) != 0)
                return BMO_RESULT_REJECTED;
            if (stats_monotonic(&prior_stats, &stats_after, report) != 0)
                return BMO_RESULT_REJECTED;
            prior_stats = stats_after;
        }
        bi_sha256_update(&sha, page, expected_page_bytes);
        report->pages_read++;
        report->covered_bytes += expected_page_bytes;
    }

    if (check_descriptor(fd, expected_dev, report, &after_stat) != 0)
        return BMO_RESULT_REJECTED;
    if (!S_ISCHR(after_stat.st_mode) || after_stat.st_rdev != expected_dev ||
        before_stat.st_dev != after_stat.st_dev ||
        before_stat.st_ino != after_stat.st_ino ||
        before_stat.st_rdev != after_stat.st_rdev ||
        (before_stat.st_mode & S_IFMT) != (after_stat.st_mode & S_IFMT))
        return reject(report, "character device identity changed during observation");
    if (query_info(fd, &info_after) != 0)
        return reject(report, "final MEMGETINFO failed");
    if (!same_info(&info_before, &info_after) ||
        info_before.type != MTD_NANDFLASH ||
        (uint64_t)info_after.size != expected_size_bytes ||
        info_after.writesize != expected_page_bytes ||
        info_after.erasesize != expected_erase_bytes)
        return reject(report, "MTD geometry changed during observation");

    if (report->pages_read != report->pages_expected ||
        report->covered_bytes != expected_size_bytes ||
        report->eraseblocks_checked != report->eraseblocks_expected)
        return reject(report, "observation coverage is incomplete");

    if (stats_state == BMO_ECC_STATS_SUPPORTED) {
        /* Capture one final cumulative counter sample after the final page. */
        stats_rc = get_stats(fd, &stats_after);
        if (stats_rc < 0)
            return reject(report, "final ECCGETSTATS failed");
        if (stats_rc > 0)
            return reject(report, "ECCGETSTATS support changed during observation");
        report->ecc_stats_samples++;
        report->ecc_corrected_after = stats_after.corrected;
        report->ecc_uncorrected_after = stats_after.failed;
        if (stats_monotonic(&prior_stats, &stats_after, report) != 0)
            return BMO_RESULT_REJECTED;
    } else if (saw_stats_unsupported) {
        report->ecc_stats_state = BMO_ECC_STATS_UNSUPPORTED;
    }

    bi_sha256_final(&sha, digest);
    bi_sha256_hex(digest, report->sha256);
    report->observation_complete = 1;
    report->binding_valid = 1;
    report->ecc_stats_state = stats_state;
    report->clean = stats_state == BMO_ECC_STATS_SUPPORTED &&
                    report->ecc_corrected_before == 0 &&
                    report->ecc_corrected_after == 0 &&
                    report->ecc_uncorrected_before == 0 &&
                    report->ecc_uncorrected_after == 0;
    if (stats_state == BMO_ECC_STATS_UNSUPPORTED) {
        set_reason(report, "complete read; ECCGETSTATS unsupported; cleanliness unknown");
        return BMO_RESULT_NON_CLEAN;
    }
    if (!report->clean) {
        set_reason(report, "complete read; corrected ECC count nonzero; report is non-clean");
        return BMO_RESULT_NON_CLEAN;
    }
    set_reason(report, "complete read; read-only binding and ECC counters clean");
    return BMO_RESULT_OK;
}

int bmo_observe_appsbl_pair(int fd0, dev_t expected_dev0,
                            int fd1, dev_t expected_dev1,
                            uint32_t expected_page_bytes,
                            uint32_t expected_erase_bytes,
                            struct bmo_appsbl_pair_report *report)
{
    int r0, r1;
    if (report == NULL)
        return BMO_RESULT_REJECTED;
    memset(report, 0, sizeof(*report));
    if (fd0 == fd1 || expected_dev0 == expected_dev1) {
        (void)snprintf(report->reason, sizeof(report->reason),
                       "APPSBL pair descriptors and expected dev_t values must be distinct");
        return BMO_RESULT_REJECTED;
    }
    r0 = bmo_observe_partition(fd0, expected_dev0, BMO_APPSBL_BYTES,
                               expected_page_bytes, expected_erase_bytes,
                               &report->appsbl[0]);
    if (r0 < 0) {
        (void)snprintf(report->reason, sizeof(report->reason),
                       "APPSBL0 observation rejected: %.120s",
                       report->appsbl[0].reason);
        return BMO_RESULT_REJECTED;
    }
    r1 = bmo_observe_partition(fd1, expected_dev1, BMO_APPSBL_BYTES,
                               expected_page_bytes, expected_erase_bytes,
                               &report->appsbl[1]);
    if (r1 < 0) {
        (void)snprintf(report->reason, sizeof(report->reason),
                       "APPSBL1 observation rejected: %.120s",
                       report->appsbl[1].reason);
        return BMO_RESULT_REJECTED;
    }
    report->observation_complete = report->appsbl[0].observation_complete &&
                                   report->appsbl[1].observation_complete;
    report->binding_valid = report->appsbl[0].binding_valid &&
                            report->appsbl[1].binding_valid;
    report->clean = report->appsbl[0].clean && report->appsbl[1].clean;
    if (!report->observation_complete || !report->binding_valid)
        return BMO_RESULT_REJECTED;
    (void)snprintf(report->reason, sizeof(report->reason), "%s",
                   report->clean ? "both APPSBL partitions observed cleanly" :
                                   "both APPSBL partitions fully read; at least one is non-clean");
    return report->clean ? BMO_RESULT_OK : BMO_RESULT_NON_CLEAN;
}
