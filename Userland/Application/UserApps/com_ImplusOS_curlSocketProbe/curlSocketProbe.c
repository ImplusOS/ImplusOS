#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../../../API/Process.h"

static int probe_fail(const char *step)
{
    printf("[curl-socket-probe] %s failed errno=%d\n", step, errno);
    return 1;
}

static void print_ipv4(const struct sockaddr_in *addr)
{
    char ip[16];
    if (!inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip))) {
        snprintf(ip, sizeof(ip), "<invalid>");
    }
    printf("[curl-socket-probe] address %s:%u\n",
           ip, (unsigned)ntohs(addr->sin_port));
}

int main(void)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    printf("[curl-socket-probe] getaddrinfo example.com:443\n");
    int gai = getaddrinfo("example.com", "443", &hints, &res);
    if (gai != 0) {
        printf("[curl-socket-probe] getaddrinfo failed: %s (%d)\n",
               gai_strerror(gai), gai);
        return 1;
    }
    const struct sockaddr_in *addr = (const struct sockaddr_in*)res->ai_addr;
    print_ipv4(addr);

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        freeaddrinfo(res);
        return probe_fail("socket");
    }
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        closesocket(fd);
        freeaddrinfo(res);
        return probe_fail("fcntl O_NONBLOCK");
    }

    printf("[curl-socket-probe] nonblocking connect\n");
    if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
        printf("[curl-socket-probe] connect completed immediately\n");
    } else if (errno != EINPROGRESS && errno != EALREADY) {
        closesocket(fd);
        freeaddrinfo(res);
        return probe_fail("connect");
    } else {
        struct pollfd pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = fd;
        pfd.events = POLLOUT | POLLERR | POLLHUP;
        printf("[curl-socket-probe] poll POLLOUT\n");
        int ready = poll(&pfd, 1, 10000);
        if (ready <= 0) {
            closesocket(fd);
            freeaddrinfo(res);
            return probe_fail("poll");
        }
        printf("[curl-socket-probe] poll revents=0x%x\n",
               (unsigned)(uint16_t)pfd.revents);
    }

    int so_error = -1;
    socklen_t so_error_len = sizeof(so_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) < 0) {
        closesocket(fd);
        freeaddrinfo(res);
        return probe_fail("getsockopt SO_ERROR");
    }
    printf("[curl-socket-probe] SO_ERROR=%d\n", so_error);
    if (so_error != 0) {
        closesocket(fd);
        freeaddrinfo(res);
        return 1;
    }

    char byte = 0;
    ssize_t n = recv(fd, &byte, sizeof(byte), MSG_DONTWAIT);
    if (n < 0 && errno == EAGAIN) {
        printf("[curl-socket-probe] recv MSG_DONTWAIT returned EAGAIN\n");
    } else {
        printf("[curl-socket-probe] recv MSG_DONTWAIT result=%ld errno=%d\n",
               (long)n, errno);
    }

    closesocket(fd);
    freeaddrinfo(res);
    printf("[curl-socket-probe] complete\n");
    return 0;
}

void _start(void)
{
    int status = main();
    process_exit(status);
    for (;;) {
        process_yield();
    }
}
