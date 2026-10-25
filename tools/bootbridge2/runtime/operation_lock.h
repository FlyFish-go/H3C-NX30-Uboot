/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_OPERATION_LOCK_H
#define BB_OPERATION_LOCK_H

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define BB_OPERATION_DIR "/tmp/bridge"
#define BB_OPERATION_LOCK BB_OPERATION_DIR "/operation.lock"

/* Keep the inode in place. The kernel releases ownership on exit/crash. */
static inline int bb_lock_path(const char *path)
{
	int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);

	if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB)) {
		int saved = errno;

		close(fd);
		errno = saved;
		return -1;
	}
	return fd;
}

static inline int bb_operation_lock(void)
{
	if (mkdir(BB_OPERATION_DIR, 0700) && errno != EEXIST)
		return -1;
	return bb_lock_path(BB_OPERATION_LOCK);
}

#endif
