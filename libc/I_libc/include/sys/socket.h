#pragma once

#include <sys/types.h>

#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_INET   2
#define AF_INET6  10

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_ERROR 4
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char __ss_padding[128 - sizeof(sa_family_t) - sizeof(unsigned long)];
    unsigned long __ss_align;
};

#define SO_BROADCAST 6
#define SO_KEEPALIVE 9
#define SO_LINGER    13

#define SOL_TCP 6
#define SOL_UDP 17

#define SOCK_CLOEXEC 0x80000
#define SOCK_NONBLOCK 0x800

int socket(int domain, int type, int protocol);
int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
int bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
ssize_t send(int sockfd, const void* buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recv(int sockfd, void* buf, size_t len, int flags);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
int closesocket(int sockfd);
int shutdown(int sockfd, int how);
int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen);
int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen);
int getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
int getpeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
int socketpair(int domain, int type, int protocol, int sv[2]);
