#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "bridge_mtd_observer.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_SIZE BMO_APPSBL_BYTES
#define TEST_ERASE 131072U
#define TEST_DEV makedev(31, 7)

enum fault_mode {
    FAULT_NONE,
    FAULT_BADBLOCK,
    FAULT_COUNTER_REGRESSION,
    FAULT_SHORT_READ,
    FAULT_DEVICE_MISMATCH,
    FAULT_UNSUPPORTED_STATS,
    FAULT_UNCORRECTABLE,
    FAULT_CHANGED_GEOMETRY,
    FAULT_CORRECTED_NONZERO,
    FAULT_EINTR_PREAD,
    FAULT_STATS_UNSUPPORTED_MIDSTREAM,
    FAULT_READONLY_MODE_CHANGE,
    FAULT_NONCHAR,
    FAULT_NOR_INFO,
    FAULT_CHANGED_INODE
};

static enum fault_mode mode;
static int fixture_fd = -1;
static int secondary_fd = -1;
static unsigned stats_calls;
static unsigned badblock_calls;
static unsigned pread_calls;
static unsigned fstat_calls;
static unsigned ioctl_calls;
static unsigned info_calls;
static unsigned fcntl_calls;
static int use_write_fd;

static int is_fixture_fd(int fd)
{
    return fd == fixture_fd || fd == secondary_fd;
}

int __real_fcntl(int fd, int cmd, ...);
int __real_fcntl64(int fd, int cmd, ...);
static int wrapped_fcntl(int fd, int cmd)
{
    if (cmd == F_GETFL && is_fixture_fd(fd)) {
        fcntl_calls++;
        return use_write_fd ? O_RDWR : O_RDONLY;
    }
    return __real_fcntl(fd, cmd);
}
int __wrap_fcntl(int fd, int cmd, ...) { return wrapped_fcntl(fd, cmd); }
int __wrap_fcntl64(int fd, int cmd, ...) { return wrapped_fcntl(fd, cmd); }

static int wrapped_fstat(int fd, struct stat *st)
{
    if (!is_fixture_fd(fd)) {
        errno = EBADF;
        return -1;
    }
    fstat_calls++;
    memset(st, 0, sizeof(*st));
    st->st_dev = makedev(8, 1);
    st->st_ino = (mode == FAULT_CHANGED_INODE && fstat_calls > 1) ? 2 : 1;
    st->st_mode = mode == FAULT_NONCHAR ? (S_IFREG | 0600) : (S_IFCHR | 0600);
    if (mode == FAULT_DEVICE_MISMATCH)
        st->st_rdev = makedev(31, 8);
    else if (fd == secondary_fd)
        st->st_rdev = makedev(31, 8);
    else
        st->st_rdev = TEST_DEV;
    return 0;
}
int __wrap_fstat(int fd, struct stat *st) { return wrapped_fstat(fd, st); }
int __wrap_fstat64(int fd, struct stat *st) { return wrapped_fstat(fd, st); }

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;
    if (!is_fixture_fd(fd)) {
        errno = EBADF;
        return -1;
    }
    ioctl_calls++;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    if (request == MEMGETINFO) {
        struct mtd_info_user *info = arg;
        info_calls++;
        memset(info, 0, sizeof(*info));
        info->type = mode == FAULT_NOR_INFO ? MTD_NORFLASH : MTD_NANDFLASH;
        info->size = TEST_SIZE;
        info->erasesize = TEST_ERASE;
        info->writesize = BMO_PAGE_BYTES;
        info->oobsize = 64;
        if (mode == FAULT_CHANGED_GEOMETRY && info_calls > 1)
            info->erasesize = TEST_ERASE * 2;
        if (mode == FAULT_CHANGED_GEOMETRY && info_calls > 1)
            info->oobsize = 128;
        return 0;
    }
    if (request == MEMGETBADBLOCK) {
        loff_t *offset = arg;
        badblock_calls++;
        return mode == FAULT_BADBLOCK && *offset == TEST_ERASE ? 1 : 0;
    }
    if (request == ECCGETSTATS) {
        struct mtd_ecc_stats *stats = arg;
        stats_calls++;
        if (mode == FAULT_UNSUPPORTED_STATS) {
            errno = ENOTTY;
            return -1;
        }
        if (mode == FAULT_STATS_UNSUPPORTED_MIDSTREAM && stats_calls == 5) {
            errno = ENOTTY;
            return -1;
        }
        stats->failed = 0;
        stats->corrected = 0;
        if (mode == FAULT_COUNTER_REGRESSION && stats_calls == 3)
            stats->corrected = 1;
        if (mode == FAULT_CORRECTED_NONZERO && stats_calls >= 3)
            stats->corrected = 2;
        if (mode == FAULT_UNCORRECTABLE && stats_calls >= 3)
            stats->failed = 1;
        return 0;
    }
    errno = ENOTTY;
    return -1;
}

static ssize_t wrapped_pread(int fd, void *buf, size_t count, off_t offset)
{
    if (!is_fixture_fd(fd) || count != BMO_PAGE_BYTES || offset < 0 ||
        (uint64_t)offset >= TEST_SIZE) {
        errno = EINVAL;
        return -1;
    }
    pread_calls++;
    if (mode == FAULT_EINTR_PREAD && pread_calls == 1) {
        errno = EINTR;
        return -1;
    }
    if (mode == FAULT_READONLY_MODE_CHANGE && pread_calls == 5)
        use_write_fd = 1;
    if (mode == FAULT_SHORT_READ && pread_calls == 11)
        return (ssize_t)(count - 1);
    memset(buf, (int)(((uint64_t)offset / BMO_PAGE_BYTES) % 251), count);
    return (ssize_t)count;
}
ssize_t __wrap_pread(int fd, void *buf, size_t count, off_t offset)
{ return wrapped_pread(fd, buf, count, offset); }
ssize_t __wrap_pread64(int fd, void *buf, size_t count, off_t offset)
{ return wrapped_pread(fd, buf, count, offset); }

static void reset_fixture(enum fault_mode next)
{
    mode = next;
    stats_calls = 0;
    badblock_calls = 0;
    pread_calls = 0;
    fstat_calls = 0;
    ioctl_calls = 0;
    info_calls = 0;
    use_write_fd = 0;
    secondary_fd = -1;
}

static int new_fixture_fd(int writable)
{
    char path[] = "/tmp/bridge-mtd-observer-fixture-XXXXXX";
    int seed = mkstemp(path);
    assert(seed >= 0);
    assert(close(seed) == 0);
    int fd = open(path, writable ? O_RDWR : O_RDONLY);
    assert(fd >= 0);
    unlink(path);
    fixture_fd = fd;
    use_write_fd = writable;
    return fd;
}

static void assert_fd_retained(int fd)
{
    assert(__real_fcntl(fd, F_GETFD) >= 0);
}

static void test_positive_and_sha(void)
{
    struct bmo_partition_report r;
    int fd;
    reset_fixture(FAULT_NONE);
    fd = new_fixture_fd(0);
    int observed = bmo_observe_partition(fd, TEST_DEV, TEST_SIZE, BMO_PAGE_BYTES,
                                         TEST_ERASE, &r);
    if (observed != BMO_RESULT_OK)
        fprintf(stderr, "positive observation returned %d: %s\n", observed, r.reason);
    assert(observed == BMO_RESULT_OK);
    assert(r.observation_complete && r.binding_valid && r.clean);
    assert(r.pages_expected == 640 && r.pages_read == 640);
    assert(r.covered_bytes == TEST_SIZE && r.eraseblocks_checked == 10);
    assert(r.badblocks == 0 && r.ecc_stats_state == BMO_ECC_STATS_SUPPORTED);
    assert(r.observed_oob_bytes == 64);
    assert(stats_calls == 1282 && badblock_calls == 10 && pread_calls == 640);
    assert(r.ecc_corrected_before == 0 && r.ecc_corrected_after == 0);
    assert(r.ecc_uncorrected_before == 0 && r.ecc_uncorrected_after == 0);
    assert(strcmp(r.sha256,
                  "29aeb6e3867ddaf33e44adcb251b14f07d276dd99330f513d0f6ec5a087b13b0") == 0);
    assert_fd_retained(fd);
    close(fd);
}

static void test_pair_reads_both_full_partitions(void)
{
    struct bmo_appsbl_pair_report pair;
    int original_fixture, fd1;
    reset_fixture(FAULT_NONE);
    original_fixture = new_fixture_fd(0);
    fd1 = dup(original_fixture);
    assert(fd1 >= 0);
    /* Both descriptors are virtual MTD fds; all I/O remains linker-wrapped. */
    secondary_fd = fd1;
    assert(bmo_observe_appsbl_pair(original_fixture, TEST_DEV, fd1, makedev(31, 8),
                                   BMO_PAGE_BYTES, TEST_ERASE, &pair) == BMO_RESULT_OK);
    assert(pair.observation_complete && pair.binding_valid && pair.clean);
    assert(pair.appsbl[0].pages_read == 640 && pair.appsbl[1].pages_read == 640);
    assert(pair.appsbl[0].covered_bytes == TEST_SIZE && pair.appsbl[1].covered_bytes == TEST_SIZE);
    assert(strcmp(pair.appsbl[0].sha256,
                  "29aeb6e3867ddaf33e44adcb251b14f07d276dd99330f513d0f6ec5a087b13b0") == 0);
    assert(strcmp(pair.appsbl[1].sha256, pair.appsbl[0].sha256) == 0);
    assert(stats_calls == 2564 && badblock_calls == 20 && pread_calls == 1280);
    assert_fd_retained(original_fixture);
    assert_fd_retained(fd1);
    close(original_fixture);
    close(fd1);
}

static void test_rejection(enum fault_mode fault, const char *contains)
{
    struct bmo_partition_report r;
    int fd;
    reset_fixture(fault);
    fd = new_fixture_fd(0);
    assert(bmo_observe_partition(fd, TEST_DEV, TEST_SIZE, BMO_PAGE_BYTES,
                                 TEST_ERASE, &r) == BMO_RESULT_REJECTED);
    assert(strstr(r.reason, contains) != NULL);
    if (fault == FAULT_COUNTER_REGRESSION) {
        assert(r.ecc_stats_state == BMO_ECC_STATS_SUPPORTED);
        assert(r.ecc_corrected_after == 0 && r.ecc_uncorrected_after == 0);
    }
    if (fault == FAULT_UNCORRECTABLE) {
        assert(r.ecc_stats_state == BMO_ECC_STATS_SUPPORTED);
        assert(r.ecc_corrected_after == 0 && r.ecc_uncorrected_after == 1);
    }
    assert_fd_retained(fd);
    close(fd);
}

static void test_write_descriptor_rejected(void)
{
    struct bmo_partition_report r;
    int fd;
    reset_fixture(FAULT_NONE);
    fd = new_fixture_fd(1);
    assert(bmo_observe_partition(fd, TEST_DEV, TEST_SIZE, BMO_PAGE_BYTES,
                                 TEST_ERASE, &r) == BMO_RESULT_REJECTED);
    assert(strstr(r.reason, "not read-only") != NULL);
    assert(pread_calls == 0 && badblock_calls == 0);
    assert_fd_retained(fd);
    close(fd);
}

static void test_pair_duplicates_rejected_without_syscalls(void)
{
    struct bmo_appsbl_pair_report pair;
    int fd;
    reset_fixture(FAULT_NONE);
    fd = new_fixture_fd(0);
    fcntl_calls = fstat_calls = ioctl_calls = pread_calls = 0;
    assert(bmo_observe_appsbl_pair(fd, TEST_DEV, fd, TEST_DEV,
                                   BMO_PAGE_BYTES, TEST_ERASE, &pair) == BMO_RESULT_REJECTED);
    assert(strstr(pair.reason, "must be distinct") != NULL);
    assert(fcntl_calls == 0 && fstat_calls == 0 && ioctl_calls == 0 && pread_calls == 0);
    assert(bmo_observe_appsbl_pair(fd, TEST_DEV, fd, makedev(31, 8),
                                   BMO_PAGE_BYTES, TEST_ERASE, &pair) == BMO_RESULT_REJECTED);
    assert(fcntl_calls == 0 && fstat_calls == 0 && ioctl_calls == 0 && pread_calls == 0);
    assert_fd_retained(fd);
    close(fd);
}

static void test_unsupported_stats_is_explicit_nonclean(void)
{
    struct bmo_partition_report r;
    int fd;
    reset_fixture(FAULT_UNSUPPORTED_STATS);
    fd = new_fixture_fd(0);
    assert(bmo_observe_partition(fd, TEST_DEV, TEST_SIZE, BMO_PAGE_BYTES,
                                 TEST_ERASE, &r) == BMO_RESULT_NON_CLEAN);
    assert(r.observation_complete && r.binding_valid && !r.clean);
    assert(r.ecc_stats_state == BMO_ECC_STATS_UNSUPPORTED);
    assert(r.pages_read == 640 && r.covered_bytes == TEST_SIZE);
    assert(strstr(r.reason, "unsupported") != NULL);
    assert_fd_retained(fd);
    close(fd);
}

static void test_corrected_is_nonclean_without_bitflip_claim(void)
{
    struct bmo_partition_report r;
    int fd;
    reset_fixture(FAULT_CORRECTED_NONZERO);
    fd = new_fixture_fd(0);
    assert(bmo_observe_partition(fd, TEST_DEV, TEST_SIZE, BMO_PAGE_BYTES,
                                 TEST_ERASE, &r) == BMO_RESULT_NON_CLEAN);
    assert(r.observation_complete && r.binding_valid && !r.clean);
    assert(r.ecc_corrected_after == 2 && r.ecc_uncorrected_after == 0);
    assert_fd_retained(fd);
    close(fd);
}

static void test_eintr_is_retried(void)
{
    struct bmo_partition_report r;
    int fd;
    reset_fixture(FAULT_EINTR_PREAD);
    fd = new_fixture_fd(0);
    assert(bmo_observe_partition(fd, TEST_DEV, TEST_SIZE, BMO_PAGE_BYTES,
                                 TEST_ERASE, &r) == BMO_RESULT_OK);
    assert(r.pages_read == 640 && pread_calls == 641);
    assert_fd_retained(fd);
    close(fd);
}

static void test_midstream_stats_support_change_is_rejected_and_reported(void)
{
    struct bmo_partition_report r;
    int fd;
    reset_fixture(FAULT_STATS_UNSUPPORTED_MIDSTREAM);
    fd = new_fixture_fd(0);
    assert(bmo_observe_partition(fd, TEST_DEV, TEST_SIZE, BMO_PAGE_BYTES,
                                 TEST_ERASE, &r) == BMO_RESULT_REJECTED);
    assert(strstr(r.reason, "support changed") != NULL);
    assert(r.ecc_stats_state == BMO_ECC_STATS_SUPPORTED);
    assert(r.ecc_corrected_after == 0 && r.ecc_uncorrected_after == 0);
    assert(r.pages_read == 1 && r.covered_bytes == BMO_PAGE_BYTES);
    assert_fd_retained(fd);
    close(fd);
}

static void test_descriptor_mutations_rejected(void)
{
    struct bmo_partition_report r;
    int fd;
    reset_fixture(FAULT_READONLY_MODE_CHANGE);
    fd = new_fixture_fd(0);
    assert(bmo_observe_partition(fd, TEST_DEV, TEST_SIZE, BMO_PAGE_BYTES,
                                 TEST_ERASE, &r) == BMO_RESULT_REJECTED);
    assert(strstr(r.reason, "not read-only") != NULL);
    assert(r.pages_read == 640 && !r.binding_valid);
    close(fd);

    reset_fixture(FAULT_CHANGED_INODE);
    fd = new_fixture_fd(0);
    assert(bmo_observe_partition(fd, TEST_DEV, TEST_SIZE, BMO_PAGE_BYTES,
                                 TEST_ERASE, &r) == BMO_RESULT_REJECTED);
    assert(strstr(r.reason, "identity changed") != NULL);
    assert(!r.binding_valid);
    close(fd);
}

int main(void)
{
    test_positive_and_sha();
    puts("PASS positive full-page observation, coverage, SHA binding, caller fd ownership");
    test_pair_reads_both_full_partitions();
    puts("PASS APPSBL pair observes 640 pages per caller-owned fd");
    test_pair_duplicates_rejected_without_syscalls();
    puts("PASS duplicate APPSBL fd/dev_t rejected before system calls");
    test_rejection(FAULT_BADBLOCK, "bad eraseblock");
    puts("PASS bad eraseblock rejected");
    test_rejection(FAULT_COUNTER_REGRESSION, "counter regressed");
    puts("PASS ECC counter regression rejected");
    test_rejection(FAULT_SHORT_READ, "short page read");
    puts("PASS short page read rejected");
    test_rejection(FAULT_DEVICE_MISMATCH, "expected dev_t");
    puts("PASS device identity mismatch rejected");
    test_write_descriptor_rejected();
    puts("PASS write-capable descriptor rejected before I/O");
    test_unsupported_stats_is_explicit_nonclean();
    puts("PASS unsupported ECCGETSTATS reported without fabricating clean status");
    test_rejection(FAULT_UNCORRECTABLE, "uncorrectable ECC");
    puts("PASS uncorrectable ECC rejected");
    test_rejection(FAULT_CHANGED_GEOMETRY, "geometry changed");
    puts("PASS changed MTD geometry and OOB size rejected");
    test_rejection(FAULT_NOR_INFO, "not NAND");
    puts("PASS NOR MTD rejected despite matching geometry");
    test_corrected_is_nonclean_without_bitflip_claim();
    puts("PASS corrected ECC count reported as non-clean only");
    test_eintr_is_retried();
    puts("PASS pread EINTR retried to exact page completion");
    test_midstream_stats_support_change_is_rejected_and_reported();
    puts("PASS ECCGETSTATS support change rejected with state retained");
    test_descriptor_mutations_rejected();
    puts("PASS read-only mode and inode changes rejected");
    test_rejection(FAULT_NONCHAR, "not a character device");
    puts("PASS non-character descriptor rejected");
    puts("RESULT 17 PASS");
    return 0;
}
