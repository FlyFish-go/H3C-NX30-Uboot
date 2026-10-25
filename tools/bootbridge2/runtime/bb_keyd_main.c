// SPDX-License-Identifier: GPL-2.0-or-later
/* bb-keyd: hold the WPS/Mesh key for 3 seconds to return to the OEM system.
 *
 * Every official H3C device tree (all products, all boot routes) declares the
 * WPS/Mesh key as gpio-keys GPIO38 / KEY_WPS_BUTTON. OEM gpio-button-hotplug
 * publishes it as BUTTON=wps on the kernel uevent socket. This works without
 * knowing the board. bb-oem-return takes the same operation lock as bbd,
 * so a busy device refuses the key action atomically. It runs every check
 * again; only on success does the device reboot. */
#include "button_event.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define RETURN_TOOL "/usr/sbin/bb-oem-return"

static int64_t now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void say(const char *msg)
{
	int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC);

	if (fd >= 0) {
		dprintf(fd, "bb-keyd: %s\n", msg);
		close(fd);
	}
}

static void do_return(void)
{
	pid_t pid;
	int status;

	say("WPS key held, returning to the OEM system");
	pid = fork();
	if (pid == 0) {
		int null = open("/dev/null", O_WRONLY);

		if (null >= 0)
			dup2(null, 1);
		execl(RETURN_TOOL, RETURN_TOOL, "commit-reboot", (char *)NULL);
		_exit(127);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) {
		say("return to OEM failed or refused; see the page for details");
		return;
	}
	/* commit-reboot only returns successfully if reboot is already underway. */
}

int main(void)
{
	struct sockaddr_nl addr = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	struct bb_button_hold hold = { .pressed_at = -1 };
	int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
			NETLINK_KOBJECT_UEVENT);
	struct pollfd pollfd = { .fd = fd, .events = POLLIN };

	if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr))) {
		say("cannot listen for kernel button events");
		return 1;
	}
	say("listening for kernel WPS/Mesh button events");
	for (;;) {
		int timeout = -1, trigger = 0;

		if (hold.pressed_at >= 0 && !hold.fired) {
			int64_t remaining = hold.pressed_at + 3000 - now_ms();

			timeout = remaining > 0 ? (int)remaining : 0;
		}
		if (poll(&pollfd, 1, timeout) < 0) {
			if (errno == EINTR) continue;
			return 1;
		}
		if (pollfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			say("button event stream interrupted");
			return 1;
		}
		while (pollfd.revents & POLLIN) {
			char buf[4096];
			struct sockaddr_nl from = {0};
			struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
			struct msghdr msg = { .msg_name = &from, .msg_namelen = sizeof(from),
					     .msg_iov = &iov, .msg_iovlen = 1 };
			ssize_t n = recvmsg(fd, &msg, MSG_DONTWAIT);
			enum bb_button_event ev;

			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
			if (n < 0 && errno == EINTR) continue;
			if (n < 0 || (msg.msg_flags & MSG_TRUNC)) {
				hold.pressed_at = -1; /* Lost events cannot prove a hold. */
				break;
			}
			if (from.nl_pid != 0) continue; /* accept only kernel events */
			ev = bb_button_parse(buf, n);
			if (ev == BB_BUTTON_PRESS) say("WPS/Mesh key pressed");
			trigger |= bb_button_step(&hold, ev, now_ms());
		}
		trigger |= bb_button_step(&hold, BB_BUTTON_NONE, now_ms());
		if (trigger) do_return();
	}
}
