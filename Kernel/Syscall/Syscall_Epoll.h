#pragma once
#include <stdint.h>

typedef struct {
    uint32_t events;
    uint64_t data;
} epoll_event_t;

int32_t syscall_epoll_create(uint64_t flags);
int32_t syscall_epoll_ctl(int32_t epfd, int32_t op, int32_t fd,
                          const epoll_event_t *event);
int32_t syscall_epoll_wait(int32_t epfd, epoll_event_t *events,
                           int32_t maxevents, int32_t timeout_ms);
int32_t syscall_eventfd(uint64_t initval, uint64_t flags);
