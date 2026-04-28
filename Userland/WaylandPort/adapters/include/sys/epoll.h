#pragma once

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
#define EPOLLIN  0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010

typedef union epoll_data {
    void *ptr;
    int fd;
    unsigned int u32;
    unsigned long long u64;
} epoll_data_t;

struct epoll_event {
    unsigned int events;
    epoll_data_t data;
};

static inline int epoll_create1(int flags) {
    long r;
    __asm__ volatile("syscall":"=a"(r):"a"(160ULL),"D"((long)flags):"rcx","r11","memory");
    return (int)r;
}
static inline int epoll_ctl(int epfd, int op, int fd, struct epoll_event *ev) {
    long r;
    register long r10 __asm__("r10") = (long)ev;
    __asm__ volatile("syscall":"=a"(r):"a"(161ULL),"D"((long)epfd),"S"((long)op),"d"((long)fd),"r"(r10):"rcx","r11","memory");
    return (int)r;
}
static inline int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    long r;
    register long r10 __asm__("r10") = (long)timeout;
    __asm__ volatile("syscall":"=a"(r):"a"(162ULL),"D"((long)epfd),"S"((long)events),"d"((long)maxevents),"r"(r10):"rcx","r11","memory");
    return (int)r;
}
