#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "bridge_mtd_observer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mtd/mtd-user.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define APPSBL_ERASE_BYTES UINT32_C(131072)

struct probe_copy {
    int fd;
    dev_t expected_dev;
    struct mtd_info_user info;
    int info_read;
    int info_errno;
    int observer_errno;
};

static int parse_uint(const char *s, unsigned int *out)
{
    unsigned long value = 0;
    const unsigned char *p = (const unsigned char *)s;
    if (*p == '\0')
        return -1;
    for (; *p; p++) {
        unsigned int digit;
        if (*p < '0' || *p > '9')
            return -1;
        digit = (unsigned int)(*p - '0');
        if (value > (UINT_MAX - digit) / 10U)
            return -1;
        value = value * 10 + digit;
    }
    *out = (unsigned int)value;
    return 0;
}

static int parse_dev(const char *s, dev_t *out)
{
    const char *colon = strchr(s, ':');
    unsigned int major_num, minor_num;
    char major_buf[32];
    size_t major_len;
    dev_t dev;
    if (!colon || strchr(colon + 1, ':'))
        return -1;
    major_len = (size_t)(colon - s);
    if (major_len == 0 || major_len >= sizeof(major_buf))
        return -1;
    memcpy(major_buf, s, major_len);
    major_buf[major_len] = '\0';
    if (parse_uint(major_buf, &major_num) != 0 ||
        parse_uint(colon + 1, &minor_num) != 0)
        return -1;
    dev = makedev(major_num, minor_num);
    if (major(dev) != major_num || minor(dev) != minor_num)
        return -1;
    *out = dev;
    return 0;
}

static int valid_node(const char *path)
{
    const char *p;
    if (strncmp(path, "/dev/mtd", 8) != 0)
        return 0;
    p = path + 8;
    if (*p == '\0')
        return 0;
    for (; *p; p++)
        if (*p < '0' || *p > '9')
            return 0;
    return 1;
}

static int open_copy(const char *path, struct probe_copy *copy)
{
    copy->fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (copy->fd < 0)
        return -1;
    memset(&copy->info, 0, sizeof(copy->info));
    if (ioctl(copy->fd, MEMGETINFO, &copy->info) == 0)
        copy->info_read = 1;
    else
        copy->info_errno = errno;
    return 0;
}

static const char *mtd_type_name(const struct probe_copy *copy)
{
    if (!copy->info_read)
        return NULL;
    switch (copy->info.type) {
    case MTD_NANDFLASH: return "MTD_NANDFLASH";
    case MTD_NORFLASH: return "MTD_NORFLASH";
    case MTD_RAM: return "MTD_RAM";
    case MTD_ROM: return "MTD_ROM";
    case MTD_DATAFLASH: return "MTD_DATAFLASH";
    default: return "MTD_OTHER";
    }
}

static void print_json_string(const char *s)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\')
            putchar('\\');
        if (*p >= 0x20)
            putchar(*p);
    }
    putchar('"');
}

static void print_copy_json(unsigned int index, const struct probe_copy *copy,
                            const struct bmo_partition_report *report,
                            const char *fallback_reason)
{
    const char *reason = report->reason[0] ? report->reason : fallback_reason;
    const char *type = mtd_type_name(copy);
    printf("    \"copy%u\": {\"type\":", index);
    if (type)
        printf("\"%s\"", type);
    else
        printf("null");
    printf(",\"oobsize\":");
    if (copy->info_read)
        printf("%u", copy->info.oobsize);
    else
        printf("null");
    printf(",\"errno\":%d,\"complete\":%s,\"binding_valid\":%s,"
           "\"clean\":%s,\"expected_bytes\":%llu,\"covered_bytes\":%llu,"
           "\"pages_read\":%u,\"pages_expected\":%u,\"eraseblocks_checked\":%u,"
           "\"ecc_stats_state\":%u,\"ecc_corrected_before\":%u,"
           "\"ecc_corrected_after\":%u,\"ecc_uncorrected_before\":%u,"
           "\"ecc_uncorrected_after\":%u,\"sha256\":",
           copy->observer_errno ? copy->observer_errno : copy->info_errno,
           report->observation_complete ? "true" : "false",
           report->binding_valid ? "true" : "false",
           report->clean ? "true" : "false",
           (unsigned long long)report->expected_bytes,
           (unsigned long long)report->covered_bytes,
           report->pages_read, report->pages_expected,
           report->eraseblocks_checked, (unsigned int)report->ecc_stats_state,
           report->ecc_corrected_before, report->ecc_corrected_after,
           report->ecc_uncorrected_before, report->ecc_uncorrected_after);
    if (report->sha256[0])
        printf("\"%s\"", report->sha256);
    else
        printf("null");
    printf(",\"reason\":");
    print_json_string(reason);
    putchar('}');
}

static int usage(void)
{
    fprintf(stderr, "usage: observer_probe /dev/mtdN major:minor /dev/mtdN major:minor\n");
    return 64;
}

int main(int argc, char **argv)
{
    struct probe_copy copies[2];
    struct bmo_appsbl_pair_report pair;
    int result, saved_errno;
    const char *fallback0, *fallback1;

    memset(copies, 0, sizeof(copies));
    copies[0].fd = copies[1].fd = -1;
    if (argc != 5 || !valid_node(argv[1]) || !valid_node(argv[3]) ||
        parse_dev(argv[2], &copies[0].expected_dev) != 0 ||
        parse_dev(argv[4], &copies[1].expected_dev) != 0 ||
        copies[0].expected_dev == copies[1].expected_dev)
        return usage();

    if (open_copy(argv[1], &copies[0]) != 0) {
        saved_errno = errno;
        fprintf(stderr, "observer_probe: open copy0 failed: %s\n", strerror(saved_errno));
        return 2;
    }
    if (open_copy(argv[3], &copies[1]) != 0) {
        saved_errno = errno;
        fprintf(stderr, "observer_probe: open copy1 failed: %s\n", strerror(saved_errno));
        close(copies[0].fd);
        return 2;
    }

    errno = 0;
    result = bmo_observe_appsbl_pair(copies[0].fd, copies[0].expected_dev,
                                     copies[1].fd, copies[1].expected_dev,
                                     BMO_PAGE_BYTES, APPSBL_ERASE_BYTES, &pair);
    saved_errno = errno;
    if (result == BMO_RESULT_REJECTED && saved_errno != 0) {
        if (strncmp(pair.reason, "APPSBL0", 7) == 0)
            copies[0].observer_errno = saved_errno;
        else if (strncmp(pair.reason, "APPSBL1", 7) == 0)
            copies[1].observer_errno = saved_errno;
    }
    fallback0 = pair.appsbl[0].reason;
    fallback1 = pair.appsbl[1].reason;
    if (!fallback0[0])
        fallback0 = pair.reason[0] ? pair.reason : "not observed";
    if (!fallback1[0])
        fallback1 = pair.reason[0] ? "not observed after other copy rejected" : "";

    printf("{\"result_code\":%d,\"pair_complete\":%s,\"pair_binding_valid\":%s,"
           "\"pair_clean\":%s,\"reason\":",
           result, pair.observation_complete ? "true" : "false",
           pair.binding_valid ? "true" : "false", pair.clean ? "true" : "false");
    print_json_string(pair.reason);
    printf(",\"copies\":{\n");
    print_copy_json(0, &copies[0], &pair.appsbl[0], fallback0);
    printf(",\n");
    print_copy_json(1, &copies[1], &pair.appsbl[1], fallback1);
    printf("\n}}\n");
    close(copies[1].fd);
    close(copies[0].fd);

    if (result == BMO_RESULT_OK)
        return 0;
    if (result == BMO_RESULT_NON_CLEAN)
        return 3;
    return 4;
}
