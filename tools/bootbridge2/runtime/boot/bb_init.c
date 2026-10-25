#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>

#define PROFILE "H3C-BOOTBRIDGE-RUNTIME-v1"
#define ROOTFS_ID "H3C-BOOTBRIDGE-ROOTFS-v1"

static const char *const child_env[] = {
    "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
    "BRIDGE_INIT_VERIFIED=H3C-BOOTBRIDGE-RUNTIME-v1",
    "BRIDGE_RUNTIME_MODE=RUNTIME_GATED",
    NULL
};

static void write_log(int fd, const char *text, size_t len) {
    while (len) {
        ssize_t written = write(fd, text, len);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return;
        text += written;
        len -= (size_t)written;
    }
}

static void logmsg(const char *msg) {
    int fd = open("/dev/console", O_WRONLY | O_NOCTTY);
    if (fd >= 0) {
        write_log(fd, msg, strlen(msg));
        write_log(fd, "\n", 1);
        close(fd);
    } else {
        write_log(STDERR_FILENO, msg, strlen(msg));
        write_log(STDERR_FILENO, "\n", 1);
    }
}

static int marker_is(const char *path, const char *expected) {
    char buf[128];
    size_t used = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    while (used < sizeof(buf) - 1) {
        ssize_t n = read(fd, buf + used, sizeof(buf) - 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); return 0; }
        if (n == 0) break;
        used += (size_t)n;
    }
    close(fd);
    if (used == sizeof(buf) - 1) return 0; /* Never validate a truncated file. */
    size_t expected_len = strlen(expected);
    /* DT strings include one final NUL. Never accept an embedded NUL followed
     * by other data as if it were an exact marker. Text markers may end in
     * ordinary whitespace; a NUL plus trailing whitespace is not a DT string. */
    if (used == expected_len + 1 && buf[expected_len] == '\0')
        return memcmp(buf, expected, expected_len) == 0;
    while (used && (buf[used - 1] == '\n' || buf[used - 1] == '\r' || buf[used - 1] == ' ' || buf[used - 1] == '\t'))
        buf[--used] = '\0';
    return used == expected_len && memcmp(buf, expected, used) == 0;
}

static int compatible_path_is_reviewed(const char *path) {
    static const char common[] = "qcom,ipq5018";
    static const char *const routes[] = {
        "qcom,ipq5018-mp02.1", "qcom,ipq5018-mp03.1", "qcom,ipq5018-mp03.1-c2"
    };
    char data[128]; size_t used = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    while (used < sizeof(data)) {
        ssize_t n = read(fd, data + used, sizeof(data) - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); return 0; }
        if (n == 0) break;
        used += (size_t)n;
    }
    close(fd);
    if (used == sizeof(data)) return 0;
    const char *end = memchr(data, '\0', used);
    if (!end) return 0;
    size_t first = (size_t)(end - data) + 1;
    if (used - first != sizeof(common) || memcmp(data + first, common, sizeof(common))) return 0;
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
        if (!strcmp(data, routes[i])) return 1;
    return 0;
}

static int target_guard(void) {
    struct utsname un;
    if (getpid() != 1) {
        static const char msg[] = "bridge-init: refusing runtime actions because this is not PID 1\n";
        write_log(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 0;
    }
    if (!marker_is("/etc/bridge/profile", PROFILE) ||
        !marker_is("/etc/bridge/rootfs-id", ROOTFS_ID)) {
        logmsg("bridge-init: bridge image markers do not match");
        return 0;
    }
    if (uname(&un) != 0 || strcmp(un.release, "4.4.60") != 0) {
        logmsg("bridge-init: expected pinned OEM kernel 4.4.60");
        return 0;
    }
    return 1;
}

static int ensure_dir(const char *path, mode_t mode) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode);
    return errno == ENOENT && mkdir(path, mode) == 0;
}

static int mount_ram(const char *source, const char *target, const char *type, unsigned long flags, const char *options) {
    if (mount(source, target, type, flags, options) == 0) return 1;
    char msg[192];
    snprintf(msg, sizeof(msg), "bridge-init: mount %s on %s failed: %s", type, target, strerror(errno));
    logmsg(msg);
    return 0;
}

static int make_node(const char *path, mode_t mode, unsigned int major_no, unsigned int minor_no) {
    struct stat st;
    if (lstat(path, &st) == 0)
        return S_ISCHR(st.st_mode) && major(st.st_rdev) == major_no && minor(st.st_rdev) == minor_no;
    if (errno != ENOENT) return 0;
    return mknod(path, S_IFCHR | mode, makedev(major_no, minor_no)) == 0;
}

static int parse_small_uint(const char **p, unsigned *out, unsigned limit) {
    unsigned value = 0, digits = 0;
    while (**p >= '0' && **p <= '9') {
        unsigned digit = (unsigned)(*(*p)++ - '0');
        if (value > (limit - digit) / 10) return 0;
        value = value * 10 + digit;
        ++digits;
    }
    if (!digits || value > limit) return 0;
    *out = value;
    return 1;
}

static int make_mtd_nodes(void) {
    DIR *dir = opendir("/sys/class/mtd");
    if (!dir) return 0;
    struct dirent *entry;
    unsigned found = 0;
    while ((entry = readdir(dir)) != NULL) {
        const char *p = entry->d_name;
        if (strncmp(p, "mtd", 3) != 0) continue;
        p += 3;
        unsigned index = 0;
        if (!parse_small_uint(&p, &index, 127) || *p != '\0') continue;
        char rel[96], value[64], node[64];
        int n = snprintf(rel, sizeof(rel), "/sys/class/mtd/mtd%u/dev", index);
        if (n <= 0 || (size_t)n >= sizeof(rel)) { closedir(dir); return 0; }
        int fd = open(rel, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) { closedir(dir); return 0; }
        ssize_t got = read(fd, value, sizeof(value) - 1);
        int read_error = got < 0 && errno != EINTR;
        if (got < 0 && errno == EINTR) got = read(fd, value, sizeof(value) - 1);
        close(fd);
        if (read_error || got <= 0 || (size_t)got >= sizeof(value) - 1) { closedir(dir); return 0; }
        value[got] = '\0';
        const char *q = value; unsigned maj = 0, min = 0;
        if (!parse_small_uint(&q, &maj, 255) || maj == 0 || *q++ != ':' ||
            !parse_small_uint(&q, &min, 255) || (*q != '\n' && *q != '\0')) {
            closedir(dir); return 0;
        }
        if (*q == '\n' && q[1] != '\0') { closedir(dir); return 0; }
        n = snprintf(node, sizeof(node), "/dev/mtd%u", index);
        if (n <= 0 || (size_t)n >= sizeof(node) || !make_node(node, 0600, maj, min)) { closedir(dir); return 0; }
        ++found;
    }
    closedir(dir);
    return found > 0;
}

static int mount_runtime(void) {
    if (!ensure_dir("/proc", 0555) || !ensure_dir("/sys", 0555) ||
        !ensure_dir("/dev", 0755) || !ensure_dir("/tmp", 01777)) {
        logmsg("bridge-init: required mountpoint missing or not a directory");
        return 0;
    }
    if (!mount_ram("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL)) return 0;
    if (!mount_ram("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL)) return 0;
    /* proc/device-tree is commonly a symlink into sysfs on this kernel. */
    if (!marker_is("/proc/device-tree/h3c,bridge-profile", "H3C-BOOTBRIDGE-MULTICONFIG-V1") &&
        !marker_is("/sys/firmware/devicetree/base/h3c,bridge-profile", "H3C-BOOTBRIDGE-MULTICONFIG-V1")) {
        logmsg("bridge-init: device-tree bridge profile marker missing or mismatched");
        return 0;
    }
    if (!compatible_path_is_reviewed("/proc/device-tree/compatible") &&
        !compatible_path_is_reviewed("/sys/firmware/devicetree/base/compatible")) {
        logmsg("bridge-init: reference route is not reviewed; refusing runtime setup");
        return 0;
    }
    if (!mount_ram("tmpfs", "/dev", "tmpfs", MS_NOSUID, "mode=0755,size=1m")) return 0;
    if (!ensure_dir("/dev/pts", 0755) || !ensure_dir("/dev/shm", 01777) ||
        !make_node("/dev/console", 0600, 5, 1) ||
        !make_node("/dev/null", 0666, 1, 3) ||
        !make_node("/dev/zero", 0666, 1, 5) ||
        !make_node("/dev/tty", 0666, 5, 0) ||
        !make_node("/dev/urandom", 0666, 1, 9)) {
        logmsg("bridge-init: cannot create required RAM device nodes");
        return 0;
    }
    int console = open("/dev/console", O_RDWR | O_NOCTTY);
    if (console >= 0) {
        (void)dup2(console, STDIN_FILENO);
        (void)dup2(console, STDOUT_FILENO);
        (void)dup2(console, STDERR_FILENO);
        if (console > STDERR_FILENO) close(console);
    }
    if (!make_mtd_nodes()) {
        logmsg("bridge-init: cannot create validated MTD character nodes from sysfs");
        return 0;
    }
    if (!mount_ram("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777,size=64m")) return 0;
    if (!ensure_dir("/tmp/run", 0755) || !ensure_dir("/tmp/bridge", 0755)) {
        logmsg("bridge-init: cannot create volatile runtime directories");
        return 0;
    }
    return 1;
}

static void plan(void) {
    puts("PID1 guard: getpid()==1; neutral /etc/bridge/profile and rootfs-id match; uname -r=4.4.60");
    puts("mount -t proc -o nosuid,noexec,nodev proc /proc");
    puts("mount -t sysfs -o nosuid,noexec,nodev sysfs /sys");
    puts("check DT bridge marker via /proc/device-tree or /sys/firmware/devicetree/base");
    puts("check reviewed DT compatible reference route; hardware UNKNOWN; RUNTIME_GATED");
    puts("mount -t tmpfs -o nosuid tmpfs /dev; create console,null,zero,tty,urandom nodes");
    puts("create only validated /dev/mtdN character nodes from numeric /sys/class/mtd/mtdN/dev major:minor values");
    puts("mount -t tmpfs -o nosuid,nodev,size=64m tmpfs /tmp; create /tmp/run and /tmp/bridge");
    puts("exec /bin/sh /etc/bridge/boot.sh once; failure => console message and PID1 stays alive");
    puts("success => start /usr/sbin/bridge-dhcp -i eth1 -s 192.168.10.1 if present and /usr/sbin/bbd -b 192.168.10.1 -p 80 -w /www concurrently");
    puts("waitpid(-1) supervises both children with bounded per-service retry delays; DHCP failure leaves static web LAN running");
}

static pid_t spawn(char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        execve(argv[0], argv, (char *const *)child_env);
        int saved_errno = errno;
        char msg[192];
        snprintf(msg, sizeof(msg), "bridge-init: exec %s failed: %s", argv[0], strerror(saved_errno));
        logmsg(msg);
        _exit(127);
    }
    return pid;
}

static void stay_alive(void) {
    for (;;) {
        while (waitpid(-1, NULL, WNOHANG) > 0) { }
        pause();
    }
}

typedef struct {
    const char *name;
    char *const *argv;
    int enabled;
    pid_t pid;
    unsigned failures;
    struct timespec restart_at;
} child_job;

static char *const dhcp_argv[] = {
    "/usr/sbin/bridge-dhcp", "-i", "eth1", "-s", "192.168.10.1", NULL
};
static char *const keyd_argv[] = {
    "/usr/sbin/bb-keyd", NULL
};
static char *const web_argv[] = {
    "/usr/sbin/bbd", "-b", "192.168.10.1", "-p", "80", "-w", "/www", NULL
};

static struct timespec now_monotonic(void) {
    struct timespec now = {0, 0};
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return now;
}

static struct timespec add_seconds(struct timespec t, unsigned seconds) {
    t.tv_sec += (time_t)seconds;
    return t;
}

static int time_reached(struct timespec now, struct timespec deadline) {
    return now.tv_sec > deadline.tv_sec ||
        (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec);
}

static unsigned restart_delay(unsigned failures) {
    if (failures >= 4) return 8;
    if (failures == 3) return 4;
    if (failures == 2) return 2;
    return 1;
}

static void alarm_wakeup(int signal_number) {
    (void)signal_number;
}

static void start_job(child_job *job) {
    pid_t pid = spawn((char *const *)job->argv);
    if (pid > 0) {
        job->pid = pid;
        return;
    }
    ++job->failures;
    unsigned delay = restart_delay(job->failures);
    job->restart_at = add_seconds(now_monotonic(), delay);
    char msg[192];
    snprintf(msg, sizeof(msg), "bridge-init: fork for %s failed; retrying in %u seconds", job->name, delay);
    logmsg(msg);
}

static void supervise(child_job *jobs, size_t count) {
    struct sigaction alarm_action;
    memset(&alarm_action, 0, sizeof(alarm_action));
    alarm_action.sa_handler = alarm_wakeup;
    sigemptyset(&alarm_action.sa_mask);
    (void)sigaction(SIGALRM, &alarm_action, NULL);

    for (size_t i = 0; i < count; ++i) {
        if (jobs[i].enabled) start_job(&jobs[i]);
    }

    for (;;) {
        struct timespec now = now_monotonic();
        unsigned alarm_seconds = 0;
        for (size_t i = 0; i < count; ++i) {
            child_job *job = &jobs[i];
            if (!job->enabled || job->pid > 0) continue;
            if (time_reached(now, job->restart_at)) {
                start_job(job);
                continue;
            }
            time_t remaining = job->restart_at.tv_sec - now.tv_sec;
            if (job->restart_at.tv_nsec > now.tv_nsec) ++remaining;
            if (remaining < 1) remaining = 1;
            if (!alarm_seconds || (unsigned)remaining < alarm_seconds) alarm_seconds = (unsigned)remaining;
        }

        if (alarm_seconds) alarm(alarm_seconds);
        int status = 0;
        pid_t pid = waitpid(-1, &status, 0);
        alarm(0);
        if (pid < 0) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) {
                /* No active children; wait only for the earliest retry slot. */
                struct timespec pause_for = {alarm_seconds ? alarm_seconds : 1, 0};
                (void)nanosleep(&pause_for, NULL);
                continue;
            }
            logmsg("bridge-init: waitpid(-1) failed; continuing supervision");
            continue;
        }

        for (size_t i = 0; i < count; ++i) {
            child_job *job = &jobs[i];
            if (job->pid != pid) continue;
            job->pid = 0;
            ++job->failures;
            unsigned delay = restart_delay(job->failures);
            job->restart_at = add_seconds(now_monotonic(), delay);
            char msg[192];
            if (strcmp(job->name, "bridge-dhcp") == 0) {
                snprintf(msg, sizeof(msg), "bridge-init: DHCP child exited; static web LAN remains available; retrying in %u seconds", delay);
            } else {
                snprintf(msg, sizeof(msg), "bridge-init: %s child exited; retrying in %u seconds", job->name, delay);
            }
            logmsg(msg);
            break;
        }
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--plan") == 0) {
        plan();
        if (getpid() == 1) stay_alive();
        return 0;
    }
    if (argc != 1) {
        if (getpid() == 1) stay_alive();
        return 125;
    }
    if (!target_guard()) {
        if (getpid() == 1) stay_alive();
        return 125;
    }
    if (!mount_runtime()) stay_alive();

    char *boot_argv[] = {"/bin/sh", "/etc/bridge/boot.sh", NULL};
    int status = 0;
    pid_t boot = spawn(boot_argv);
    pid_t waited = boot < 0 ? -1 : waitpid(boot, &status, 0);
    if (boot < 0 || waited != boot || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        char msg[192];
        if (boot < 0)
            snprintf(msg, sizeof(msg), "bridge-init: boot setup fork failed: %s", strerror(errno));
        else if (waited != boot)
            snprintf(msg, sizeof(msg), "bridge-init: boot setup wait failed: %s", strerror(errno));
        else if (WIFEXITED(status))
            snprintf(msg, sizeof(msg), "bridge-init: boot setup exited with status %d", WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            snprintf(msg, sizeof(msg), "bridge-init: boot setup terminated by signal %d", WTERMSIG(status));
        else
            snprintf(msg, sizeof(msg), "bridge-init: unexpected boot setup status %d", status);
        logmsg(msg);
        logmsg("bridge-init: bridge boot setup failed; remaining alive without reboot");
        stay_alive();
    }

    child_job jobs[] = {
        {"bridge-dhcp", dhcp_argv, access(dhcp_argv[0], X_OK) == 0, 0, 0, {0, 0}},
        {"bbd", web_argv, access(web_argv[0], X_OK) == 0, 0, 0, {0, 0}},
        {"bb-keyd", keyd_argv, access(keyd_argv[0], X_OK) == 0, 0, 0, {0, 0}}
    };
    if (!jobs[1].enabled) {
        logmsg("bridge-init: bbd missing; remaining alive without reboot");
        stay_alive();
    }
    supervise(jobs, sizeof(jobs) / sizeof(jobs[0]));
    return 0;
}
