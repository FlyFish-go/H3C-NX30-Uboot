// SPDX-License-Identifier: GPL-2.0-or-later
#include "../runtime/operation_lock.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

int main(void)
{
	char path[] = "/tmp/bb-lock-test-XXXXXX";
	int fd = mkstemp(path), lock, status;
	pid_t pid;

	if (fd < 0)
		return 1;
	close(fd);
	lock = bb_lock_path(path);
	if (lock < 0)
		return 1;
	pid = fork();
	if (pid == 0) {
		int second;

		close(lock);
		second = bb_lock_path(path);
		_exit(second < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) ? 0 : 1);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status))
		return 1;
	close(lock);
	/* A new owner can acquire the same inode; exit releases without cleanup. */
	pid = fork();
	if (pid == 0)
		_exit(bb_lock_path(path) >= 0 ? 0 : 1);
	if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status))
		return 1;
	lock = bb_lock_path(path);
	if (lock < 0)
		return 1;
	close(lock);
	unlink(path);
	if (bb_lock_path("/no-such-bb-directory/lock") >= 0)
		return 1;
	puts("operation lock: concurrent writer refused, reacquire/exit release/io refusal PASS");
	return 0;
}
