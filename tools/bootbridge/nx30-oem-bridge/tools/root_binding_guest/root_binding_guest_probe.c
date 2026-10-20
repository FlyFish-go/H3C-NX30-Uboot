/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Live-guest probe: facts come only from mounted guest sysfs/proc and stat. */
#define _GNU_SOURCE
#include "bridge_root_binding.h"
#if !defined(BI_ROOT_BINDING_CONTRACT_HEADER)
#error "the guest probe requires the build-generated root-binding contract header"
#endif
#include BI_ROOT_BINDING_CONTRACT_HEADER
#if !defined(BI_ROOT_BINDING_CONTRACT)
#error "the root-binding contract header must define BI_ROOT_BINDING_CONTRACT"
#endif
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/reboot.h>
#include <unistd.h>

static char root_event[1024], block_name[64], block_type[64], block_path[PATH_MAX];
static char mtd_name[128], mtd_type[64], volume_name[128], volume_type[64];
static char mount_source[256], mount_target[256], mount_type[64], mount_options[512];
static char cmdline[4096];
static const br_gluebi_contract expected_contract = BI_ROOT_BINDING_CONTRACT;

static int read_text(const char *path, char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    size_t n;
    int extra;
    if (!f) return -1;
    if (cap < 2) { fclose(f); return -1; }
    n = fread(out, 1, cap - 1, f);
    if (ferror(f)) { fclose(f); return -1; }
    extra = fgetc(f);
    if (extra != EOF) { fclose(f); return -1; }
    if (fclose(f) || n == 0) return -1;
    out[n] = 0;
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = 0;
    return 0;
}

static int read_u64(const char *path, uint64_t *out)
{
    char value[128], *end;
    unsigned long long parsed;
    if (read_text(path, value, sizeof(value))) return -1;
    errno = 0;
    parsed = strtoull(value, &end, 0);
    if (errno || end == value || *end) return -1;
    *out = (uint64_t)parsed;
    return 0;
}

static int read_key(const char *text, const char *key, char *out, size_t cap)
{
    size_t keylen = strlen(key);
    unsigned matches = 0;
    char copy[sizeof(root_event)], *save = NULL, *line;
    if (strlen(text) >= sizeof(copy)) return -1;
    strcpy(copy, text);
    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, key, keylen) && line[keylen] == '=') {
            size_t n = strlen(line + keylen + 1);
            if (++matches != 1 || n >= cap) return -1;
            memcpy(out, line + keylen + 1, n + 1);
        }
    }
    return matches == 1 ? 0 : -1;
}

static int exact_numbered(const char *name, const char *prefix, unsigned *a, unsigned *b)
{
    int consumed = 0, count;
    if (!strcmp(prefix, "ubi")) {
        count = sscanf(name, "ubi%u%n", a, &consumed);
        if (count != 1 || name[consumed]) return 0;
        return 1;
    }
    if (!strcmp(prefix, "vol")) {
        count = sscanf(name, "ubi%u_%u%n", a, b, &consumed);
        if (count != 2 || name[consumed]) return 0;
        return 1;
    }
    if (!strcmp(prefix, "mtd")) {
        count = sscanf(name, "mtd%u%n", a, &consumed);
        if (count != 1 || name[consumed]) return 0;
        return 1;
    }
    return 0;
}

static int one_root_mount(void)
{
    FILE *f = fopen("/proc/self/mounts", "r");
    char line[2048];
    unsigned roots = 0;
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        char source[256], target[256], type[64], options[512];
        if (sscanf(line, "%255s %255s %63s %511s", source, target, type, options) != 4)
            continue;
        if (strcmp(target, "/")) continue;
        if (++roots != 1) { fclose(f); return -1; }
        strcpy(mount_source, source); strcpy(mount_target, target);
        strcpy(mount_type, type); strcpy(mount_options, options);
    }
    if (ferror(f) || fclose(f)) return -1;
    return roots == 1 ? 0 : -1;
}

static int collect(br_gluebi_evidence *e)
{
    struct stat root_stat;
    char path[PATH_MAX], resolved[PATH_MAX], dev[64], *devices;
    unsigned block_idx, mtd_idx = 0, ubi_id = 0, ubi_mtd = 0, vol_ubi = 0, vol_id = 0;
    unsigned mtd_matches = 0, proc_mtd_matches = 0, attach_count = 0, volume_matches = 0;
    uint64_t value;
    DIR *dir = NULL;
    struct dirent *de;
    FILE *f = NULL;

    memset(e, 0, sizeof(*e));
    if (stat("/", &root_stat)) return -1;
    e->root_major = major(root_stat.st_dev); e->root_minor = minor(root_stat.st_dev);
    snprintf(path, sizeof(path), "/sys/dev/block/%u:%u/uevent", e->root_major, e->root_minor);
    if (read_text(path, root_event, sizeof(root_event)) ||
        read_key(root_event, "MAJOR", dev, sizeof(dev))) return -1;
    e->event_major = (uint32_t)strtoul(dev, NULL, 10);
    if (read_key(root_event, "MINOR", dev, sizeof(dev))) return -1;
    e->event_minor = (uint32_t)strtoul(dev, NULL, 10);
    if (read_key(root_event, "DEVNAME", block_name, sizeof(block_name)) ||
        read_key(root_event, "DEVTYPE", block_type, sizeof(block_type))) return -1;
    if (sscanf(block_name, "mtdblock%u%c", &block_idx, (char[2]){0}) != 1) return -1;
    e->block_mtd_index = block_idx;
    snprintf(path, sizeof(path), "/sys/dev/block/%u:%u", e->root_major, e->root_minor);
    if (!realpath(path, resolved) || !(devices = strstr(resolved, "/sys/devices/"))) return -1;
    strcpy(block_path, devices);
    e->block_devname = block_name; e->block_devtype = block_type; e->block_sysfs_path = block_path;

    dir = opendir("/sys/class/mtd");
    if (!dir) return -1;
    while ((de = readdir(dir))) {
        unsigned index;
        if (!exact_numbered(de->d_name, "mtd", &index, NULL)) continue;
        snprintf(path, sizeof(path), "/sys/class/mtd/%s/name", de->d_name);
        if (read_text(path, dev, sizeof(dev))) { closedir(dir); return -1; }
        if (strcmp(dev, "ubi_rootfs")) continue;
        if (++mtd_matches != 1) { closedir(dir); return -1; }
        mtd_idx = index;
        strcpy(mtd_name, dev);
#define READ_MTD_FIELD(field, target) do { \
    snprintf(path, sizeof(path), "/sys/class/mtd/%s/%s", de->d_name, field); \
    if (read_text(path, dev, sizeof(dev))) { closedir(dir); return -1; } \
    (target) = (uint64_t)strtoull(dev, NULL, 0); \
} while (0)
        snprintf(path, sizeof(path), "/sys/class/mtd/%s/type", de->d_name);
        if (read_text(path, mtd_type, sizeof(mtd_type))) { closedir(dir); return -1; }
        snprintf(path, sizeof(path), "/sys/class/mtd/%s/dev", de->d_name);
        if (read_text(path, dev, sizeof(dev)) || sscanf(dev, "%u:%u", &e->mtd_major, &e->mtd_minor) != 2) { closedir(dir); return -1; }
        READ_MTD_FIELD("size", e->mtd_size);
        READ_MTD_FIELD("erasesize", e->mtd_erase);
        READ_MTD_FIELD("writesize", e->mtd_write);
#undef READ_MTD_FIELD
    }
    if (closedir(dir) || mtd_matches != 1) return -1;
    if (mtd_idx != block_idx) return -1;
    e->mtd_name_matches = mtd_matches; e->mtd_name = mtd_name; e->mtd_type = mtd_type;

    f = fopen("/proc/mtd", "r");
    if (!f) return -1;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char name[256];
        unsigned row_index;
        unsigned long long row_size, row_erase;
        if (sscanf(line, "mtd%u: %llx %llx \"%255[^\"]\"",
                   &row_index, &row_size, &row_erase, name) != 4) continue;
        if (strcmp(name, "ubi_rootfs")) continue;
        if (++proc_mtd_matches != 1 || row_index != block_idx ||
            row_size != e->mtd_size || row_erase != e->mtd_erase) {
            fclose(f);
            return -1;
        }
    }
    if (ferror(f) || fclose(f)) return -1;
    e->proc_mtd_name_matches = proc_mtd_matches;

    dir = opendir("/sys/class/ubi");
    if (!dir) return -1;
    while ((de = readdir(dir))) {
        unsigned u = 0, v = 0;
        if (exact_numbered(de->d_name, "ubi", &u, NULL)) {
            if (++attach_count != 1) { closedir(dir); return -1; }
            snprintf(path, sizeof(path), "/sys/class/ubi/%s/mtd_num", de->d_name);
            if (read_text(path, dev, sizeof(dev))) { closedir(dir); return -1; }
            ubi_id = u; ubi_mtd = (unsigned)strtoul(dev, NULL, 0);
        } else if (exact_numbered(de->d_name, "vol", &u, &v)) {
            snprintf(path, sizeof(path), "/sys/class/ubi/%s/name", de->d_name);
            if (read_text(path, dev, sizeof(dev))) { closedir(dir); return -1; }
            if (strcmp(dev, "ubi_rootfs")) continue;
            if (++volume_matches != 1) { closedir(dir); return -1; }
            vol_ubi = u; vol_id = v;
            strcpy(volume_name, dev);
            snprintf(path, sizeof(path), "/sys/class/ubi/%s/type", de->d_name);
            if (read_text(path, volume_type, sizeof(volume_type))) { closedir(dir); return -1; }
#define READ_UBI_FIELD(field, target) do { \
    snprintf(path, sizeof(path), "/sys/class/ubi/%s/%s", de->d_name, field); \
    if (read_u64(path, &value)) { closedir(dir); return -1; } \
    (target) = value; \
} while (0)
            READ_UBI_FIELD("alignment", e->volume_alignment);
            READ_UBI_FIELD("corrupted", e->volume_corrupted);
            READ_UBI_FIELD("upd_marker", e->volume_update_marker);
            READ_UBI_FIELD("data_bytes", e->volume_data_bytes);
            READ_UBI_FIELD("reserved_ebs", e->volume_reserved_ebs);
#undef READ_UBI_FIELD
    snprintf(path, sizeof(path), "/sys/class/ubi/ubi%u_%u/usable_eb_size", u, v);
            if (read_u64(path, &e->usable_leb_size)) { closedir(dir); return -1; }
        }
    }
    if (closedir(dir) || attach_count != 1 || volume_matches != 1) return -1;
    if (ubi_mtd == mtd_idx) return -1;
    snprintf(path, sizeof(path), "/sys/class/mtd/mtd%u/name", ubi_mtd);
    if (read_text(path, dev, sizeof(dev))) return -1;
    char underlying_mtd_name[128];
    if (strlen(dev) >= sizeof(underlying_mtd_name)) return -1;
    strcpy(underlying_mtd_name, dev);
    f = fopen("/proc/mtd", "r");
    if (!f) return -1;
    {
        char line2[512], expected[32];
        unsigned underlying_matches = 0;
        snprintf(expected, sizeof(expected), "mtd%u", ubi_mtd);
        while (fgets(line2, sizeof(line2), f)) {
            char name[256], row_name[32];
            unsigned row_index;
            unsigned long long row_size, row_erase;
            if (sscanf(line2, "mtd%u: %llx %llx \"%255[^\"]\"",
                       &row_index, &row_size, &row_erase, name) != 4) continue;
            snprintf(row_name, sizeof(row_name), "mtd%u", row_index);
            if (strcmp(row_name, expected)) continue;
            if (++underlying_matches != 1 || !row_size || !row_erase ||
                strcmp(name, underlying_mtd_name)) {
                fclose(f);
                return -1;
            }
        }
        if (ferror(f) || fclose(f) || underlying_matches != 1) return -1;
    }
    e->attached_ubi_count = attach_count; e->ubi_id = ubi_id; e->ubi_mtd_index = ubi_mtd;
    e->root_volume_name_matches = volume_matches; e->volume_ubi_id = vol_ubi;
    e->volume_id = vol_id; e->volume_name = volume_name; e->volume_type = volume_type;

    if (one_root_mount() || read_text("/proc/cmdline", cmdline, sizeof(cmdline))) return -1;
    e->mount_source = mount_source; e->mount_target = mount_target;
    e->mount_type = mount_type; e->mount_options = mount_options; e->cmdline = cmdline;
    return 0;
}

int main(void)
{
    br_gluebi_evidence evidence;
    uint32_t attached = UINT32_MAX;
    int result;
    struct utsname kernel;
    if (collect(&evidence)) {
        fprintf(stderr, "FAIL: cannot collect live guest sysfs/proc/mount facts: %s\n", strerror(errno));
        return 2;
    }
    result = br_validate_gluebi_root(&evidence, &expected_contract, &attached);
    if (uname(&kernel)) strcpy(kernel.release, "unknown");
    printf("R3_GATE status=%s result=%d attached_mtd=%" PRIu32 " kernel=%s root_dev=%" PRIu32 ":%" PRIu32
           " block=%s block_type=%s block_sysfs=%s mtd=%" PRIu32 ":%" PRIu32 ":%s:%s:%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64
           " ubi=%" PRIu32 ":mtd%u volume=%" PRIu32 ":%s:%s:%" PRIu64 ":%" PRIu64 ":%" PRIu32 ":%" PRIu32 ":%" PRIu32
           " contract=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64
           " mount=%s:%s:%s options=%s cmdline=%s\n",
           result ? "FAIL" : "PASS", result, attached, kernel.release,
           evidence.root_major, evidence.root_minor, evidence.block_devname,
           evidence.block_devtype, evidence.block_sysfs_path,
           evidence.mtd_major, evidence.mtd_minor, evidence.mtd_name, evidence.mtd_type,
           evidence.mtd_size, evidence.mtd_erase, evidence.mtd_write, evidence.usable_leb_size,
           evidence.ubi_id, evidence.ubi_mtd_index, evidence.volume_id, evidence.volume_name,
           evidence.volume_type, evidence.volume_data_bytes, evidence.volume_reserved_ebs,
           evidence.volume_alignment, evidence.volume_corrupted, evidence.volume_update_marker,
           expected_contract.volume_bytes, expected_contract.usable_leb_bytes,
           expected_contract.reserved_ebs, expected_contract.mtd_write_bytes,
           evidence.mount_source, evidence.mount_target, evidence.mount_type, evidence.mount_options,
           evidence.cmdline);
    fflush(stdout);
    sync();
    if (reboot(RB_POWER_OFF)) {
        fprintf(stderr, "FAIL: guest poweroff syscall failed: %s\n", strerror(errno));
        return 3;
    }
    return result ? 1 : 0;
}
