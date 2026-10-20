#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

int observer_probe_main(int argc, char **argv);

enum fixture_mode { MODE_CLEAN, MODE_NONCLEAN, MODE_REJECT };
static enum fixture_mode selected_mode;
static int fd0 = -1;
static int fd1 = -1;

static int fd_index(int fd)
{
    if (fd == fd0) return 0;
    if (fd == fd1) return 1;
    return -1;
}

int __real_open(const char *path, int flags, ...);
static int wrap_open_common(const char *path, int flags)
{
    int index;
    int fd;
    if (strcmp(path, "/dev/mtd0") == 0)
        index = 0;
    else if (strcmp(path, "/dev/mtd1") == 0)
        index = 1;
    else {
        errno = ENOENT;
        return -1;
    }
    fd = __real_open("/dev/null", flags);
    if (fd < 0)
        return -1;
    if (index == 0) fd0 = fd;
    else fd1 = fd;
    return fd;
}

int __wrap_open(const char *path, int flags, ...) { return wrap_open_common(path, flags); }
int __wrap_open64(const char *path, int flags, ...) { return wrap_open_common(path, flags); }

int __real_fcntl(int fd, int cmd, ...);
int __real_fcntl64(int fd, int cmd, ...);
static int wrap_fcntl_common(int fd, int cmd)
{
    if (fd_index(fd) >= 0 && cmd == F_GETFL)
        return O_RDONLY;
    return __real_fcntl(fd, cmd);
}
int __wrap_fcntl(int fd, int cmd, ...) { return wrap_fcntl_common(fd, cmd); }
int __wrap_fcntl64(int fd, int cmd, ...) { return wrap_fcntl_common(fd, cmd); }

int __real_fstat(int fd, struct stat *st);
int __real_fstat64(int fd, struct stat *st);
static int wrap_fstat_common(int fd, struct stat *st)
{
    int index = fd_index(fd);
    if (index < 0) {
        errno = EBADF;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_dev = makedev(8, 1);
    st->st_ino = (ino_t)(100 + index);
    st->st_mode = S_IFCHR | 0600;
    st->st_rdev = index == 0 ? makedev(31, 7) : makedev(31, 8);
    return 0;
}
int __wrap_fstat(int fd, struct stat *st) { return wrap_fstat_common(fd, st); }
int __wrap_fstat64(int fd, struct stat *st) { return wrap_fstat_common(fd, st); }

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;
    int index = fd_index(fd);
    if (index < 0) {
        errno = EBADF;
        return -1;
    }
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    if (request == MEMGETINFO) {
        struct mtd_info_user *info = arg;
        memset(info, 0, sizeof(*info));
        info->type = MTD_NANDFLASH;
        info->size = 0x140000;
        info->erasesize = 131072;
        info->writesize = 2048;
        info->oobsize = 64;
        return 0;
    }
    if (request == MEMGETBADBLOCK) {
        int64_t *offset = arg;
        return selected_mode == MODE_REJECT && index == 1 && *offset == 0 ? 1 : 0;
    }
    if (request == ECCGETSTATS) {
        struct mtd_ecc_stats *stats = arg;
        stats->corrected = selected_mode == MODE_NONCLEAN && index == 1 ? 1 : 0;
        stats->failed = 0;
        return 0;
    }
    errno = ENOTTY;
    return -1;
}

ssize_t __wrap_pread(int fd, void *buf, size_t count, off_t offset)
{
    int index = fd_index(fd);
    if (index < 0 || count != 2048 || offset < 0 || offset >= 0x140000) {
        errno = EINVAL;
        return -1;
    }
    memset(buf, index == 0 ? 0x11 : 0x22, count);
    return (ssize_t)count;
}
ssize_t __wrap_pread64(int fd, void *buf, size_t count, off_t offset)
{ return __wrap_pread(fd, buf, count, offset); }

int main(int argc, char **argv)
{
    char *probe_argv[] = {
        (char *)"observer_probe",
        (char *)"/dev/mtd0", (char *)"31:7",
        (char *)"/dev/mtd1", (char *)"31:8", NULL
    };
    if (argc != 2)
        return 64;
    if (strcmp(argv[1], "clean") == 0)
        selected_mode = MODE_CLEAN;
    else if (strcmp(argv[1], "nonclean") == 0)
        selected_mode = MODE_NONCLEAN;
    else if (strcmp(argv[1], "reject") == 0)
        selected_mode = MODE_REJECT;
    else
        return 64;
    return observer_probe_main(5, probe_argv);
}
