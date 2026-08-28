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
/* Same as syscall_epoll_wait(), but also reports (via *should_switch_out)
 * whether the caller should request a scheduler switch - see the design
 * note at the top of Syscall_Epoll.c for why this can't just block
 * internally. */
int32_t syscall_epoll_wait_ex(int32_t epfd, epoll_event_t *events,
                              int32_t maxevents, int32_t timeout_ms,
                              int *should_switch_out);
int32_t syscall_eventfd(uint64_t initval, uint64_t flags);
int  syscall_eventfd_is_valid(int32_t fd);
int64_t syscall_eventfd_read(int32_t fd, uint8_t *buffer, uint64_t len);
int64_t syscall_eventfd_write(int32_t fd, const uint8_t *buffer, uint64_t len);
int32_t syscall_eventfd_close(int32_t fd);
