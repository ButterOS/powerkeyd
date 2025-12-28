
#define _GNU_SOURCE
#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <libevdev/libevdev.h>

static volatile sig_atomic_t g_stop = 0;

static void on_sig(int sig) {
    (void)sig;
    g_stop = 1;
}

static long long monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int systemctl_start(const char* unit) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execl("/bin/systemctl", "systemctl", "start", unit, (char*)NULL);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

static bool is_power_key_code(unsigned int code) {
    return (code == KEY_POWER || code == KEY_SLEEP || code == KEY_WAKEUP);
}

/* screen off heuristic: brightness == 0 */
static bool is_brightness_zero(void) {
    FILE* fp = popen("sh -lc 'cat /sys/class/backlight/*/brightness 2>/dev/null | head -n1'", "r");
    if (!fp) return false;
    char buf[64] = {0};
    if (!fgets(buf, sizeof(buf), fp)) {
        pclose(fp);
        return false;
    }
    pclose(fp);
    long v = strtol(buf, NULL, 10);
    return (v == 0);
}

static int open_matching_input(const char* desired_name_substr, bool try_grab) {
    for (int i = 0; i < 64; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);

        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;

        struct libevdev* dev = NULL;
        int rc = libevdev_new_from_fd(fd, &dev);
        if (rc < 0) {
            close(fd);
            continue;
        }

        const char* name = libevdev_get_name(dev);
        bool name_ok = true;
        if (desired_name_substr && desired_name_substr[0] != '\0') {
            name_ok = (name && strstr(name, desired_name_substr));
        }

        bool has_power =
            libevdev_has_event_code(dev, EV_KEY, KEY_POWER) ||
            libevdev_has_event_code(dev, EV_KEY, KEY_SLEEP) ||
            libevdev_has_event_code(dev, EV_KEY, KEY_WAKEUP);

        libevdev_free(dev);

        if (name_ok && has_power) {
            if (try_grab) {
                close(fd);
                fd = open(path, O_RDWR | O_CLOEXEC);
                if (fd >= 0) {
                    (void)ioctl(fd, EVIOCGRAB, (void*)1);
                }
            }
            return fd;
        }

        close(fd);
    }
    return -1;
}

int main(int argc, char** argv) {
    const double short_max_s = 0.7;
    const double long_min_s  = 1.5;

    const char* match = (argc >= 2) ? argv[1] : DEFAULT_MATCH;
    bool grab = DEFAULT_GRAB;

    if (argc >= 3 && strcmp(argv[2], "--nograb") == 0) {
        grab = false;
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    int fd = open_matching_input(match, grab);
    if (fd < 0) {
        fprintf(stderr, "powerkeyd: no matching input device found (match='%s')\\n", match);
        return 1;
    }

    struct libevdev* dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        fprintf(stderr, "powerkeyd: libevdev_new_from_fd failed: %s\\n", strerror(-rc));
        close(fd);
        return 1;
    }

    fprintf(stderr, "powerkeyd %s: using input '%s'\\n",
            PACKAGE_VERSION, libevdev_get_name(dev));

    long long pressed_at_ms = -1;

    while (!g_stop) {
        struct input_event ev;
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        if (rc == -EAGAIN) {
            struct pollfd pfd = {.fd = fd, .events = POLLIN};
            poll(&pfd, 1, 250);
            continue;
        }
        if (rc < 0) {
            fprintf(stderr, "powerkeyd: read error: %s\\n", strerror(-rc));
            break;
        }
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            while ((rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &ev))
                   == LIBEVDEV_READ_STATUS_SYNC) { }
            continue;
        }

        if (ev.type != EV_KEY) continue;
        if (!is_power_key_code((unsigned)ev.code)) continue;

        if (ev.value == 1) { /* key down */
            pressed_at_ms = monotonic_ms();
        } else if (ev.value == 0) { /* key up */
            if (pressed_at_ms < 0) continue;
            long long dt_ms = monotonic_ms() - pressed_at_ms;
            pressed_at_ms = -1;

            double dt_s = (double)dt_ms / 1000.0;

            if (dt_s >= long_min_s) {
                systemctl_start("os-wake.service");
                systemctl_start("os-wlogout.service");
            } else if (dt_s <= short_max_s) {
                if (is_brightness_zero()) {
                    systemctl_start("os-wake.service");
                } else {
                    systemctl_start("os-screenoff.service");
                }
            }
        }
    }

    libevdev_free(dev);
    close(fd);
    return 0;
}
