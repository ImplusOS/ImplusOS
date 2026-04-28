#include "Evdev_Client.h"
#include "Sync/Spinlock.h"
#include "Timer/Timer.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    uint8_t used;
    input_event_t ring[EVDEV_RING_SIZE];
    uint32_t head, tail;
    spinlock_t lock;
} evdev_device_t;

static evdev_device_t g_devs[EVDEV_MAX_DEVICES];
static int g_evdev_init_done = 0;

void evdev_init(void) {
    memset(g_devs, 0, sizeof(g_devs));
    for (int i = 0; i < EVDEV_MAX_DEVICES; i++) spinlock_init(&g_devs[i].lock);
    g_devs[0].used = 1;
    g_devs[1].used = 1;
    g_evdev_init_done = 1;
}

static evdev_device_t *evdev_get(int32_t fd) {
    int idx = fd - EVDEV_FD_BASE;
    if (idx < 0 || idx >= EVDEV_MAX_DEVICES) return NULL;
    return g_devs[idx].used ? &g_devs[idx] : NULL;
}

static void evdev_push(evdev_device_t *dev, uint16_t type, uint16_t code, int32_t value) {
    spinlock_lock(&dev->lock);
    uint32_t next = (dev->head + 1) % EVDEV_RING_SIZE;
    if (next != dev->tail) {
        uint32_t hz = timer_hz(); if (!hz) hz = 60;
        uint64_t ms = (timer_ticks() * 1000ULL) / hz;
        dev->ring[dev->head].time_sec = ms / 1000;
        dev->ring[dev->head].time_usec = (ms % 1000) * 1000;
        dev->ring[dev->head].type = type;
        dev->ring[dev->head].code = code;
        dev->ring[dev->head].value = value;
        dev->head = next;
    }
    spinlock_unlock(&dev->lock);
}

void evdev_push_key_event(uint16_t code, int32_t value) {
    if (!g_evdev_init_done) evdev_init();
    evdev_push(&g_devs[0], EV_KEY, code, value);
    evdev_push(&g_devs[0], EV_SYN, SYN_REPORT, 0);
}

void evdev_push_rel_event(uint16_t code, int32_t value) {
    if (!g_evdev_init_done) evdev_init();
    evdev_push(&g_devs[1], EV_REL, code, value);
}

void evdev_push_abs_event(uint16_t code, int32_t value) {
    if (!g_evdev_init_done) evdev_init();
    evdev_push(&g_devs[1], EV_ABS, code, value);
}

int64_t evdev_open(const char *path) {
    if (!g_evdev_init_done) evdev_init();
    (void)path;
    for (int i = 0; i < EVDEV_MAX_DEVICES; i++) {
        if (g_devs[i].used) return EVDEV_FD_BASE + i;
    }
    return -19;
}

int64_t evdev_read(int32_t fd, void *buf, uint64_t len) {
    evdev_device_t *dev = evdev_get(fd);
    if (!dev || !buf) return -14;
    uint64_t ev_size = sizeof(input_event_t);
    uint64_t count = 0;
    input_event_t *out = (input_event_t*)buf;
    spinlock_lock(&dev->lock);
    while (count + ev_size <= len && dev->tail != dev->head) {
        out[count / ev_size] = dev->ring[dev->tail];
        dev->tail = (dev->tail + 1) % EVDEV_RING_SIZE;
        count += ev_size;
    }
    spinlock_unlock(&dev->lock);
    return (int64_t)count;
}

int64_t evdev_ioctl(int32_t fd, uint64_t request, uint64_t arg) {
    (void)fd; (void)request; (void)arg;
    return 0;
}

int64_t evdev_close(int32_t fd) {
    (void)fd;
    return 0;
}
