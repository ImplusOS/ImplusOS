#include "../include/posix_net.h"
#include "../include/posix_fdtable.h"
#include "../include/posix_errno.h"
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern int32_t socket_create (int32_t type);
extern int32_t socket_connect(int32_t sockfd, uint32_t ip, uint16_t port);
extern int32_t socket_bind   (int32_t sockfd, uint16_t port);
extern int32_t socket_listen (int32_t sockfd);
extern int32_t socket_accept (int32_t sockfd);
extern int32_t socket_send   (int32_t sockfd, const void *data, uint32_t len);
extern int32_t socket_recv   (int32_t sockfd, void *buf, uint32_t buf_len);
extern int32_t socket_close  (int32_t sockfd);

uint16_t posix_htons(uint16_t h)
{
    return (uint16_t)((h << 8) | (h >> 8));
}

uint16_t posix_ntohs(uint16_t n)
{
    return posix_htons(n);
}

uint32_t posix_htonl(uint32_t h)
{
    return ((h & 0x000000FFu) << 24) |
           ((h & 0x0000FF00u) <<  8) |
           ((h & 0x00FF0000u) >>  8) |
           ((h & 0xFF000000u) >> 24);
}

uint32_t posix_ntohl(uint32_t n)
{
    return posix_htonl(n);
}

int posix_inet_aton(const char *cp, struct in_addr *inp)
{
    if (!cp || !inp) {
        errno = EINVAL;
        return 0;
    }

    uint32_t parts[4];
    int      npart = 0;
    const char *p  = cp;

    while (npart < 4) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
        uint32_t v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (uint32_t)(*p - '0');
            p++;
        }
        if (v > 255) {
            return 0;
        }
        parts[npart++] = v;
        if (npart < 4) {
            if (*p != '.') return 0;
            p++;
        }
    }

    if (*p != '\0') {
        return 0;
    }

    inp->s_addr = posix_htonl(
        (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]);
    return 1;
}

in_addr_t posix_inet_addr(const char *cp)
{
    struct in_addr addr;
    if (!posix_inet_aton(cp, &addr)) {
        return (in_addr_t)0xFFFFFFFFu;
    }
    return addr.s_addr;
}

char *posix_inet_ntoa(struct in_addr in)
{
    static char buf[16];
    uint32_t v = posix_ntohl(in.s_addr);
    uint8_t  a = (uint8_t)((v >> 24) & 0xFF);
    uint8_t  b = (uint8_t)((v >> 16) & 0xFF);
    uint8_t  c = (uint8_t)((v >>  8) & 0xFF);
    uint8_t  d = (uint8_t)( v        & 0xFF);
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d);
    return buf;
}

 

int posix_socket(int domain, int type, int protocol)
{
    (void)protocol;
    if (domain != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    int32_t fd = socket_create((int32_t)type);
    if (fd < 0) {
        posix_set_errno_from_status((int64_t)fd);
        return -1;
    }
    posix_fd_open((int)fd, POSIX_FD_TYPE_SOCKET, 0);
    os_errno = 0;
    return (int)fd;
}

 

int posix_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    (void)addrlen;
    if (!addr) {
        errno = EINVAL;
        return -1;
    }
    const struct sockaddr_in *in_addr = (const struct sockaddr_in *)addr;
    if (in_addr->sin_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    uint16_t port = posix_ntohs(in_addr->sin_port);
    int32_t r = socket_bind((int32_t)sockfd, port);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    os_errno = 0;
    return 0;
}

 

int posix_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    (void)addrlen;
    if (!addr) {
        errno = EINVAL;
        return -1;
    }
    const struct sockaddr_in *in_addr = (const struct sockaddr_in *)addr;
    if (in_addr->sin_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    uint32_t ip   = posix_ntohl(in_addr->sin_addr.s_addr);
    uint16_t port = posix_ntohs(in_addr->sin_port);
    int32_t r = socket_connect((int32_t)sockfd, ip, port);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    os_errno = 0;
    return 0;
}

 

int posix_listen(int sockfd, int backlog)
{
    (void)backlog;
    int32_t r = socket_listen((int32_t)sockfd);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    os_errno = 0;
    return 0;
}

 

int posix_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
     
    if (addr && addrlen && *addrlen >= sizeof(struct sockaddr_in)) {
        memset(addr, 0, sizeof(struct sockaddr_in));
        *addrlen = sizeof(struct sockaddr_in);
    }
    int32_t fd = socket_accept((int32_t)sockfd);
    if (fd < 0) {
        posix_set_errno_from_status((int64_t)fd);
        return -1;
    }
    posix_fd_open((int)fd, POSIX_FD_TYPE_SOCKET, 0);
    os_errno = 0;
    return (int)fd;
}

 

ssize_t posix_send(int sockfd, const void *buf, size_t len, int flags)
{
    (void)flags;
    if (!buf) {
        errno = EINVAL;
        return -1;
    }
    int32_t r = socket_send((int32_t)sockfd, buf, (uint32_t)len);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    os_errno = 0;
    return (ssize_t)r;
}

 

ssize_t posix_recv(int sockfd, void *buf, size_t len, int flags)
{
    (void)flags;
    if (!buf) {
        errno = EINVAL;
        return -1;
    }
    int32_t r = socket_recv((int32_t)sockfd, buf, (uint32_t)len);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    os_errno = 0;
    return (ssize_t)r;
}

 

ssize_t posix_sendto(int sockfd, const void *buf, size_t len, int flags,
                     const struct sockaddr *dest_addr, socklen_t addrlen)
{
    (void)flags;
    (void)addrlen;
    if (!buf) {
        errno = EINVAL;
        return -1;
    }
     
    posix_fd_entry_t *e = posix_fd_entry(sockfd);
    if (e && e->type == POSIX_FD_TYPE_SOCKET) {
        if (dest_addr) {
             
            posix_connect(sockfd, dest_addr, addrlen);
        }
    }
    return posix_send(sockfd, buf, len, 0);
}

 

ssize_t posix_recvfrom(int sockfd, void *buf, size_t len, int flags,
                       struct sockaddr *src_addr, socklen_t *addrlen)
{
    (void)flags;
    if (src_addr && addrlen) {
        memset(src_addr, 0, *addrlen);
    }
    return posix_recv(sockfd, buf, len, 0);
}

 

int posix_shutdown(int sockfd, int how)
{
    (void)how;
     
    int32_t r = socket_close((int32_t)sockfd);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    posix_fd_close(sockfd);
    os_errno = 0;
    return 0;
}

 

int posix_closesocket(int sockfd)
{
    int32_t r = socket_close((int32_t)sockfd);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    posix_fd_close(sockfd);
    os_errno = 0;
    return 0;
}

 

int posix_setsockopt(int sockfd, int level, int optname,
                     const void *optval, socklen_t optlen)
{
    (void)sockfd; (void)level; (void)optname; (void)optval; (void)optlen;
     
    os_errno = 0;
    return 0;
}

 

int posix_getsockopt(int sockfd, int level, int optname,
                     void *optval, socklen_t *optlen)
{
    (void)sockfd; (void)level;
    if (optname == SO_ERROR && optval &&
        optlen && *optlen >= (socklen_t)sizeof(int))
    {
        *(int *)optval = 0;
        *optlen = (socklen_t)sizeof(int);
        os_errno = 0;
        return 0;
    }
    errno = ENOTSUP;
    return -1;
}

 

int posix_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    (void)sockfd;
    if (!addr || !addrlen || *addrlen < sizeof(struct sockaddr_in)) {
        errno = EINVAL;
        return -1;
    }
    memset(addr, 0, sizeof(struct sockaddr_in));
    ((struct sockaddr_in *)addr)->sin_family  = AF_INET;
    *addrlen = sizeof(struct sockaddr_in);
    os_errno = 0;
    return 0;
}

 

int posix_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return posix_getsockname(sockfd, addr, addrlen);
}
