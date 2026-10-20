/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Call the kernel mount syscall with the literal production source string. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    unsigned i;
    struct stat st;
    const char *source = "mtd:ubi_rootfs";
    (void)argv;
    if (argc != 1) return 2;
    if (symlink("/dev/mtdblock24", "/mtd:ubi_rootfs") && errno != EEXIST) {
        fprintf(stderr, "R3_MOUNT_FAIL step=symlink errno=%d %s\n", errno, strerror(errno));
        return 1;
    }
    for (i = 0; i < 100; ++i) {
        if (!stat("/sys/dev/block/31:24/uevent", &st) && !access("/dev/mtdblock24", R_OK))
            break;
        usleep(100000);
    }
    if (i == 100) {
        fprintf(stderr, "R3_MOUNT_FAIL step=wait-device sysfs=/sys/dev/block/31:24/uevent\n");
        return 1;
    }
    if (mount(source, "/newroot", "squashfs", MS_RDONLY, NULL)) {
        fprintf(stderr, "R3_MOUNT_FAIL step=mount source=%s errno=%d %s\n",
                source, errno, strerror(errno));
        return 1;
    }
    printf("R3_MOUNT status=PASS source=%s target=/newroot type=squashfs flags=MS_RDONLY\n", source);
    fflush(stdout);
    return 0;
}
