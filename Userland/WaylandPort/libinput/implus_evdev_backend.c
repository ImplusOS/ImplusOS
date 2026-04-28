#include "implus_evdev.h"
#include <stdint.h>
#include <stddef.h>

static int g_evdev_fds[4] = {-1,-1,-1,-1};
static int g_evdev_count = 0;

int implus_libinput_open_device(const char *path) {
    if (g_evdev_count >= 4) return -1;
    int fd = implus_evdev_open(path);
    if (fd >= 0) g_evdev_fds[g_evdev_count++] = fd;
    return fd;
}

long implus_libinput_read_events(int fd, struct input_event *buf, int max_events) {
    return implus_evdev_read(fd, buf, (unsigned long)(max_events * sizeof(struct input_event)));
}

int implus_libinput_close_device(int fd) {
    for (int i = 0; i < g_evdev_count; i++) {
        if (g_evdev_fds[i] == fd) {
            g_evdev_fds[i] = g_evdev_fds[--g_evdev_count];
            break;
        }
    }
    return implus_evdev_close(fd);
}
