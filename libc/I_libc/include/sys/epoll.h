#pragma once

#include <sys/types.h>

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLLIN  0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010
#define EPOLLET  0x80000000

typedef union epoll_data {
    void    *ptr;
    int      fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

/* Linux's x86_64 struct epoll_event is PACKED: 12 bytes, with `data` at
 * offset 4, not 16 bytes with `data` at offset 8. Getting this wrong makes
 * every epoll_wait() hand userland the `data` it registered shifted by four
 * bytes and the array stride wrong by four -- which is how the X server's
 * ospoll_wait() read a NULL `struct ospollfd *` out of events[i].data.ptr and
 * died dereferencing it (CR2=0x18) the moment it entered its main loop.
 * See TODO_Doom_Xorg_MethodA.md M21. */
#if defined(__x86_64__)
struct __attribute__((packed)) epoll_event {
    uint32_t     events;
    epoll_data_t data;
};
#else
struct epoll_event {
    uint32_t     events;
    epoll_data_t data;
};
#endif

int epoll_create(int size);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
int epoll_create1(int flags);
