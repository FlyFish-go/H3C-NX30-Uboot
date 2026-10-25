// SPDX-License-Identifier: GPL-2.0-or-later
/* bb-keyd: hold the WPS/Mesh key for 3 seconds to return to the OEM system.
 *
 * Every official H3C device tree (all products, all boot routes) declares the
 * WPS/Mesh key as gpio-keys GPIO38 / KEY_WPS_BUTTON, so this works without
 * knowing the board. The key is ignored while bbd is writing flash
 * (/tmp/bridge/busy exists). The return itself is bb-oem-return, which runs
 * every check again; only on success does the device reboot. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define HOLD_MS 3000
#define MAX_DEV 8
#define BUSY_FLAG "/tmp/bridge/busy"
#define RETURN_TOOL "/usr/sbin/bb-oem-return"

static long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}

static void say(const char *msg)
{
	int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC);

	if (fd >= 0) {
		dprintf(fd, "bb-keyd: %s\n", msg);
		close(fd);
	}
}

/* /dev is a tiny tmpfs without input nodes: create them from sysfs. */
static int open_inputs(struct pollfd *fds)
{
	DIR *d = opendir("/sys/class/input");
	struct dirent *e;
	int n = 0;

	mkdir("/tmp/bridge", 0755);
	if (!d)
		return 0;
	while ((e = readdir(d)) && n < MAX_DEV) {
		char path[128], node[64];
		unsigned maj, min;
		FILE *f;

		if (strncmp(e->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "/sys/class/input/%s/dev", e->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (fscanf(f, "%u:%u", &maj, &min) != 2) {
			fclose(f);
			continue;
		}
		fclose(f);
		snprintf(node, sizeof(node), "/tmp/bridge/%s", e->d_name);
		unlink(node);
		if (mknod(node, S_IFCHR | 0600, makedev(maj, min)))
			continue;
		fds[n].fd = open(node, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
		if (fds[n].fd >= 0)
			fds[n++].events = POLLIN;
	}
	closedir(d);
	return n;
}

static void do_return(void)
{
	pid_t pid;
	int status;

	if (access(BUSY_FLAG, F_OK) == 0) {
		say("flash operation in progress, key ignored");
		return;
	}
	say("WPS key held, returning to the OEM system");
	pid = fork();
	if (pid == 0) {
		int null = open("/dev/null", O_WRONLY);

		if (null >= 0)
			dup2(null, 1);
		execl(RETURN_TOOL, RETURN_TOOL, "commit", (char *)NULL);
		_exit(127);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) {
		say("return to OEM failed or refused; see the page for details");
		return;
	}
	say("BOOTCONFIG now selects the OEM system, rebooting");
	sync();
	sleep(1);
	reboot(RB_AUTOBOOT);
}

int main(void)
{
	struct pollfd fds[MAX_DEV];
	long pressed_at = -1;
	int n, fired = 0;

	while ((n = open_inputs(fds)) == 0)
		sleep(5);	/* gpio-keys may probe after us */
	for (;;) {
		int timeout = pressed_at < 0 ? -1 : (int)(pressed_at + HOLD_MS - now_ms());

		if (pressed_at >= 0 && timeout <= 0) {
			if (!fired)
				do_return();
			fired = 1;
			timeout = -1;
		}
		if (poll(fds, n, timeout) < 0 && errno != EINTR)
			return 1;
		for (int i = 0; i < n; i++) {
			struct input_event ev;

			if (!(fds[i].revents & POLLIN))
				continue;
			while (read(fds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.type != EV_KEY || ev.code != KEY_WPS_BUTTON)
					continue;
				if (ev.value == 1 && pressed_at < 0) {
					pressed_at = now_ms();
					fired = 0;
				} else if (ev.value == 0) {
					pressed_at = -1;
				}
			}
		}
	}
}
