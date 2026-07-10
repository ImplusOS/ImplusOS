#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

#include "utils/errors.h"

#define NETSURF_SCHEDULE_RUN_BUDGET 64u
#define NETSURF_SCHEDULE_MIN_WAIT_MS 25
#define NETSURF_SCHEDULE_MAX_WAIT_MS 100

typedef struct netsurf_schedule_node {
    struct netsurf_schedule_node *next;
    struct timeval due;
    void (*callback)(void *p);
    void *p;
} netsurf_schedule_node_t;

static netsurf_schedule_node_t *g_schedule_list;

static int timeval_cmp(const struct timeval *a, const struct timeval *b)
{
    if (a->tv_sec < b->tv_sec) return -1;
    if (a->tv_sec > b->tv_sec) return 1;
    if (a->tv_usec < b->tv_usec) return -1;
    if (a->tv_usec > b->tv_usec) return 1;
    return 0;
}

static void timeval_add_ms(struct timeval *tv, int ms)
{
    if (ms < 0) {
        ms = 0;
    }

    tv->tv_sec += (time_t)(ms / 1000);
    tv->tv_usec += (suseconds_t)((ms % 1000) * 1000);
    while (tv->tv_usec >= 1000000) {
        tv->tv_sec++;
        tv->tv_usec -= 1000000;
    }
}

static int timeval_delta_ms(const struct timeval *future,
                            const struct timeval *now)
{
    if (timeval_cmp(future, now) <= 0) {
        return 0;
    }

    long sec = (long)(future->tv_sec - now->tv_sec);
    long usec = (long)(future->tv_usec - now->tv_usec);
    long ms = sec * 1000 + usec / 1000;

    if (ms <= 0) {
        return 1;
    }
    if (ms > NETSURF_SCHEDULE_MAX_WAIT_MS) {
        return NETSURF_SCHEDULE_MAX_WAIT_MS;
    }
    return (int)ms;
}

static nserror schedule_remove(void (*callback)(void *p), void *p)
{
    netsurf_schedule_node_t *cur = g_schedule_list;
    netsurf_schedule_node_t *prev = NULL;

    while (cur != NULL) {
        if (cur->callback == callback && cur->p == p) {
            netsurf_schedule_node_t *old = cur;

            cur = old->next;
            if (prev == NULL) {
                g_schedule_list = cur;
            } else {
                prev->next = cur;
            }
            free(old);
        } else {
            prev = cur;
            cur = cur->next;
        }
    }

    return NSERROR_OK;
}

nserror __wrap_framebuffer_schedule(int tival, void (*callback)(void *p),
                                    void *p)
{
    nserror ret = schedule_remove(callback, p);
    if (tival < 0 || ret != NSERROR_OK) {
        return ret;
    }

    netsurf_schedule_node_t *node =
        (netsurf_schedule_node_t *)calloc(1, sizeof(*node));
    if (node == NULL) {
        return NSERROR_NOMEM;
    }

    gettimeofday(&node->due, NULL);
    timeval_add_ms(&node->due, tival);
    node->callback = callback;
    node->p = p;
    node->next = g_schedule_list;
    g_schedule_list = node;
    return NSERROR_OK;
}

int __wrap_schedule_run(void)
{
    struct timeval now;
    struct timeval next_due;
    bool have_next = false;
    uint32_t ran = 0u;

    if (g_schedule_list == NULL) {
        return -1;
    }

    gettimeofday(&now, NULL);

    for (;;) {
        netsurf_schedule_node_t *cur = g_schedule_list;
        netsurf_schedule_node_t *prev = NULL;
        bool fired = false;

        while (cur != NULL) {
            if (timeval_cmp(&now, &cur->due) >= 0) {
                netsurf_schedule_node_t *old = cur;
                void (*callback)(void *p) = old->callback;
                void *arg = old->p;

                cur = old->next;
                if (prev == NULL) {
                    g_schedule_list = cur;
                } else {
                    prev->next = cur;
                }

                free(old);
                callback(arg);
                ran++;
                fired = true;
                break;
            }

            prev = cur;
            cur = cur->next;
        }

        if (!fired || ran >= NETSURF_SCHEDULE_RUN_BUDGET) {
            break;
        }
        gettimeofday(&now, NULL);
    }

    for (netsurf_schedule_node_t *cur = g_schedule_list;
         cur != NULL;
         cur = cur->next) {
        if (!have_next || timeval_cmp(&cur->due, &next_due) < 0) {
            next_due = cur->due;
            have_next = true;
        }
    }

    if (!have_next) {
        return NETSURF_SCHEDULE_MAX_WAIT_MS;
    }

    int wait_ms = timeval_delta_ms(&next_due, &now);
    if (wait_ms <= 0) {
        return ran >= NETSURF_SCHEDULE_RUN_BUDGET ? NETSURF_SCHEDULE_MIN_WAIT_MS
                                                 : NETSURF_SCHEDULE_MAX_WAIT_MS;
    }
    if (wait_ms < NETSURF_SCHEDULE_MIN_WAIT_MS) {
        wait_ms = NETSURF_SCHEDULE_MIN_WAIT_MS;
    }
    return wait_ms;
}
