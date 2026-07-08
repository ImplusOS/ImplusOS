#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../../../API/Process.h"

static void probe_log(const char *line)
{
    printf("%s\n", line);
}

static void probe_logf(const char *fmt, ...)
{
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    probe_log(line);
}

static int probe_fail(const char *step)
{
    probe_logf("[curl-socket-probe] %s failed errno=%d", step, errno);
    return 1;
}

static void print_ipv4(const struct sockaddr_in *addr)
{
    char ip[16];
    if (!inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip))) {
        snprintf(ip, sizeof(ip), "<invalid>");
    }
    probe_logf("[curl-socket-probe] address %s:%u",
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

    const char *node = "example.com";
    const char *service = "80";

    probe_log("[curl-socket-probe] getaddrinfo example.com:80");
    int gai = getaddrinfo(node, service, &hints, &res);
    if (gai != 0) {
        probe_logf("[curl-socket-probe] getaddrinfo failed: %s (%d)",
                   gai_strerror(gai), gai);
        probe_log("[curl-socket-probe] fallback getaddrinfo 10.0.2.2:8000");
        node = "10.0.2.2";
        service = "8000";
        gai = getaddrinfo(node, service, &hints, &res);
        if (gai != 0) {
            probe_logf("[curl-socket-probe] fallback getaddrinfo failed: %s (%d)",
                       gai_strerror(gai), gai);
            return 1;
        }
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

    probe_log("[curl-socket-probe] nonblocking connect");
    if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
        probe_log("[curl-socket-probe] connect completed immediately");
    } else if (errno != EINPROGRESS && errno != EALREADY) {
        closesocket(fd);
        freeaddrinfo(res);
        return probe_fail("connect");
    } else {
        struct pollfd pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = fd;
        pfd.events = POLLOUT | POLLERR | POLLHUP;
        probe_log("[curl-socket-probe] poll POLLOUT");
        int ready = poll(&pfd, 1, 10000);
        if (ready <= 0) {
            closesocket(fd);
            freeaddrinfo(res);
            return probe_fail("poll");
        }
        probe_logf("[curl-socket-probe] poll revents=0x%x",
                   (unsigned)(uint16_t)pfd.revents);
    }

    int so_error = -1;
    socklen_t so_error_len = sizeof(so_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) < 0) {
        closesocket(fd);
        freeaddrinfo(res);
        return probe_fail("getsockopt SO_ERROR");
    }
    probe_logf("[curl-socket-probe] SO_ERROR=%d", so_error);
    if (so_error != 0) {
        closesocket(fd);
        freeaddrinfo(res);
        return 1;
    }

    char request[192];
    snprintf(request, sizeof(request),
        "%s%s\r\n"
        "User-Agent: ImplusOS-netprobe\r\n"
        "Connection: close\r\n"
        "\r\n",
        "GET / HTTP/1.0\r\nHost: ", node);
    ssize_t sent = send(fd, request, strlen(request), MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent < 0) {
        closesocket(fd);
        freeaddrinfo(res);
        return probe_fail("send HTTP request");
    }
    probe_logf("[curl-socket-probe] sent HTTP request bytes=%ld", (long)sent);

    char response[256];
    int saw_data = 0;
    uint64_t deadline = get_uptime_ms() + 10000u;
    while (get_uptime_ms() < deadline) {
        struct pollfd pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = fd;
        pfd.events = POLLIN | POLLERR | POLLHUP;
        int ready = poll(&pfd, 1, 250);
        if (ready < 0) {
            closesocket(fd);
            freeaddrinfo(res);
            return probe_fail("poll response");
        }
        if (ready == 0) {
            continue;
        }
        probe_logf("[curl-socket-probe] response poll revents=0x%x",
                   (unsigned)(uint16_t)pfd.revents);
        ssize_t n = recv(fd, response, sizeof(response) - 1u, MSG_DONTWAIT);
        if (n > 0) {
            response[n] = '\0';
            probe_logf("[curl-socket-probe] received HTTP bytes=%ld", (long)n);
            char *line_end = strchr(response, '\n');
            if (line_end) *line_end = '\0';
            probe_logf("[curl-socket-probe] first line: %s", response);
            saw_data = 1;
            break;
        }
        if (n == 0) {
            probe_log("[curl-socket-probe] peer closed before data");
            break;
        }
        if (errno != EAGAIN) {
            closesocket(fd);
            freeaddrinfo(res);
            return probe_fail("recv response");
        }
    }
    if (!saw_data) {
        closesocket(fd);
        freeaddrinfo(res);
        probe_log("[curl-socket-probe] timed out waiting for HTTP response");
        return 1;
    }

    closesocket(fd);
    freeaddrinfo(res);
    probe_log("[curl-socket-probe] complete");
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
