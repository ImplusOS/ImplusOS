 

#include "../include/posix_io.h"
#include "../include/posix_fdtable.h"
#include "../include/posix_errno.h"
#include "../include/posix_file.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

 

extern void sleep_ms(uint64_t ms);

 
#ifndef F_GETFD
#define F_GETFD    1
#define F_SETFD    2
#define F_GETFL    3
#define F_SETFL    4
#define F_DUPFD_CLOEXEC 1030
#endif

#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK 0x0800
#endif

 

int posix_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);

    posix_fd_entry_t *e = posix_fd_entry(fd);
    if (!e || !e->valid) {
        va_end(ap);
        errno = EBADF;
        return -1;
    }

    int ret = -1;

    switch (cmd) {
        case F_GETFL:
            ret = e->status_flags;
            break;

        case F_SETFL:
            e->status_flags = va_arg(ap, int);
            ret = 0;
            break;

        case F_GETFD:
            ret = e->fd_flags;
            break;

        case F_SETFD:
            e->fd_flags = va_arg(ap, int);
            ret = 0;
            break;

        case F_DUPFD_CLOEXEC: {
            (void)va_arg(ap, int);  
            int newfd = posix_dup(fd);
            if (newfd >= 0) {
                posix_fd_entry_t *ne = posix_fd_entry(newfd);
                if (ne) {
                    ne->fd_flags |= FD_CLOEXEC;
                }
            }
            ret = newfd;
            break;
        }

        default:
            errno = ENOTSUP;
            ret = -1;
            break;
    }

    va_end(ap);
    if (ret >= 0) os_errno = 0;
    return ret;
}

 

int posix_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);

    if (request == FIONBIO) {
        int *param = va_arg(ap, int *);
        va_end(ap);
        if (!param) {
            errno = EINVAL;
            return -1;
        }

        int flags = posix_fcntl(fd, F_GETFL);
        if (flags < 0) {
            return -1;
        }
        if (*param) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        return posix_fcntl(fd, F_SETFL, flags);
    }

    if (request == FIONREAD) {
        int *out = va_arg(ap, int *);
        va_end(ap);
         
        if (out) *out = 1;
        return 0;
    }

    va_end(ap);
    errno = ENOTSUP;
    return -1;
}

 

int posix_select(int nfds, fd_set *readfds, fd_set *writefds,
                 fd_set *exceptfds, struct timeval *timeout)
{
     
    if (timeout) {
        uint64_t ms = (uint64_t)timeout->tv_sec * 1000ULL +
                      (uint64_t)(timeout->tv_usec / 1000ULL);
        if (ms > 0) {
            sleep_ms(ms);
        }
    }

    int ready = 0;

     
    for (int fd = 0; fd < nfds && fd < FD_SETSIZE; fd++) {
        if (!posix_fd_is_valid(fd)) {
            if (    (readfds   && FD_ISSET(fd, readfds))  ||
                    (writefds  && FD_ISSET(fd, writefds)) ||
                    (exceptfds && FD_ISSET(fd, exceptfds)) )
            {
                 
                if (readfds)   FD_CLR(fd, readfds);
                if (writefds)  FD_CLR(fd, writefds);
                if (exceptfds) FD_CLR(fd, exceptfds);
            }
            continue;
        }

        if (readfds && FD_ISSET(fd, readfds)) {
            ready++;
        }
        if (writefds && FD_ISSET(fd, writefds)) {
            ready++;
        }
        if (exceptfds) {
            FD_CLR(fd, exceptfds);   
        }
    }

    os_errno = 0;
    return ready;
}

 

int posix_poll(struct pollfd *fds, nfds_t nfds, int timeout_ms)
{
    if (timeout_ms > 0) {
        sleep_ms((uint64_t)timeout_ms);
    }

    int ready = 0;
    for (nfds_t i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        if (!posix_fd_is_valid(fds[i].fd)) {
            fds[i].revents = POLLERR;
            continue;
        }
         
        if (fds[i].events & POLLIN) {
            fds[i].revents |= POLLIN;
        }
        if (fds[i].events & POLLOUT) {
            fds[i].revents |= POLLOUT;
        }
        if (fds[i].revents) {
            ready++;
        }
    }

    os_errno = 0;
    return ready;
}
