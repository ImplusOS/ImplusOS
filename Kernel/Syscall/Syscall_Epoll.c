#include "Syscall_Main.h"
#include "../Common/Status.h"
#include "../Sync/Spinlock.h"

#include <stddef.h>
#include <stdint.h>

#define EPOLL_MAX_INSTANCES   16
#define EPOLL_MAX_ENTRIES     64
#define EVENTFD_MAX_INSTANCES 32
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLLIN      0x001u
#define EPOLLOUT     0x004u
#define EPOLLERR     0x008u
#define EPOLLHUP     0x010u
#define EPOLLET      (1u << 31)

typedef struct {
    uint32_t events;
    uint64_t data;
} epoll_event_t;

typedef struct {
    int      fd;
    uint32_t events;
    uint64_t data;
} epoll_entry_t;

typedef struct {
    uint8_t       used;
    uint32_t      count;
    epoll_entry_t entries[EPOLL_MAX_ENTRIES];
} epoll_instance_t;

typedef struct {
    uint8_t  used;
    uint64_t counter;
    int      flags;
} eventfd_instance_t;

static epoll_instance_t  g_epoll_instances[EPOLL_MAX_INSTANCES];
static eventfd_instance_t g_eventfd_instances[EVENTFD_MAX_INSTANCES];
static spinlock_t        g_epoll_lock;
static spinlock_t        g_eventfd_lock;
static int               g_epoll_initialized = 0;

static void epoll_ensure_init(void)
{
    if (!g_epoll_initialized) {
        spinlock_init(&g_epoll_lock);
        spinlock_init(&g_eventfd_lock);
        for (int i = 0; i < EPOLL_MAX_INSTANCES; ++i) {
            g_epoll_instances[i].used  = 0;
            g_epoll_instances[i].count = 0;
        }
        for (int i = 0; i < EVENTFD_MAX_INSTANCES; ++i) {
            g_eventfd_instances[i].used = 0;
        }
        g_epoll_initialized = 1;
    }
}

int32_t syscall_epoll_create(uint64_t flags)
{
    (void)flags;
    epoll_ensure_init();
    spinlock_lock(&g_epoll_lock);
    for (int i = 0; i < EPOLL_MAX_INSTANCES; ++i) {
        if (!g_epoll_instances[i].used) {
            g_epoll_instances[i].used  = 1;
            g_epoll_instances[i].count = 0;
            spinlock_unlock(&g_epoll_lock);
            return (int32_t)(0x4000 + i);
        }
    }
    spinlock_unlock(&g_epoll_lock);
    return -24;
}

static epoll_instance_t *epoll_lookup(int32_t epfd)
{
    int idx = epfd - 0x4000;
    if (idx < 0 || idx >= EPOLL_MAX_INSTANCES) return NULL;
    return g_epoll_instances[idx].used ? &g_epoll_instances[idx] : NULL;
}

int32_t syscall_epoll_ctl(int32_t epfd, int32_t op, int32_t fd,
                          const epoll_event_t *event)
{
    epoll_ensure_init();
    spinlock_lock(&g_epoll_lock);
    epoll_instance_t *inst = epoll_lookup(epfd);
    if (!inst) {
        spinlock_unlock(&g_epoll_lock);
        return -9;
    }

    if (op == EPOLL_CTL_ADD) {
        if (inst->count >= EPOLL_MAX_ENTRIES) {
            spinlock_unlock(&g_epoll_lock);
            return -12;
        }
        epoll_entry_t *e = &inst->entries[inst->count++];
        e->fd     = fd;
        e->events = event ? event->events : (EPOLLIN | EPOLLOUT);
        e->data   = event ? event->data : 0;
    } else if (op == EPOLL_CTL_DEL) {
        for (uint32_t i = 0; i < inst->count; ++i) {
            if (inst->entries[i].fd == fd) {
                inst->entries[i] = inst->entries[--inst->count];
                break;
            }
        }
    } else if (op == EPOLL_CTL_MOD) {
        for (uint32_t i = 0; i < inst->count; ++i) {
            if (inst->entries[i].fd == fd) {
                inst->entries[i].events = event ? event->events : (EPOLLIN | EPOLLOUT);
                inst->entries[i].data   = event ? event->data : 0;
                break;
            }
        }
    }

    spinlock_unlock(&g_epoll_lock);
    return 0;
}

int32_t syscall_epoll_wait(int32_t epfd, epoll_event_t *events,
                           int32_t maxevents, int32_t timeout_ms)
{
    (void)timeout_ms;
    epoll_ensure_init();

    if (!events || maxevents <= 0) {
        return -22;
    }

    spinlock_lock(&g_epoll_lock);
    epoll_instance_t *inst = epoll_lookup(epfd);
    if (!inst) {
        spinlock_unlock(&g_epoll_lock);
        return -9;
    }

    int32_t n = 0;
    for (uint32_t i = 0; i < inst->count && n < maxevents; ++i) {
        events[n].events = inst->entries[i].events & (EPOLLIN | EPOLLOUT);
        events[n].data   = inst->entries[i].data;
        ++n;
    }
    spinlock_unlock(&g_epoll_lock);
    return n;
}

int32_t syscall_eventfd(uint64_t initval, uint64_t flags)
{
    epoll_ensure_init();
    spinlock_lock(&g_eventfd_lock);
    for (int i = 0; i < EVENTFD_MAX_INSTANCES; ++i) {
        if (!g_eventfd_instances[i].used) {
            g_eventfd_instances[i].used    = 1;
            g_eventfd_instances[i].counter = initval;
            g_eventfd_instances[i].flags   = (int)flags;
            spinlock_unlock(&g_eventfd_lock);
            return (int32_t)(0x5000 + i);
        }
    }
    spinlock_unlock(&g_eventfd_lock);
    return -24;
}
