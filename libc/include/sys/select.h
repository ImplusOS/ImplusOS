#pragma once

#include <sys/types.h>
#include <string.h>
#include <sys/time.h>

typedef struct {
    unsigned long fds_bits[4];
} fd_set;

#define FD_ZERO(set) memset((set), 0, sizeof(fd_set))
#define FD_SET(fd, set)   ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] |=  (1UL << ((fd) % (8 * sizeof(unsigned long)))))
#define FD_CLR(fd, set)   ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] &= ~(1UL << ((fd) % (8 * sizeof(unsigned long)))))
#define FD_ISSET(fd, set) (((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] &   (1UL << ((fd) % (8 * sizeof(unsigned long))))) != 0)

int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
           struct timeval* timeout);
