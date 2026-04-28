#include "implus_unix_socket.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

int implus_wl_socket(void) {
    return implus_unix_socket(1);
}

int implus_wl_bind(int fd, const char *path) {
    return implus_unix_bind(fd, path);
}

int implus_wl_listen(int fd) {
    return implus_unix_listen(fd, 16);
}

int implus_wl_accept(int fd) {
    return implus_unix_accept(fd);
}

int implus_wl_connect(int fd, const char *path) {
    return implus_unix_connect(fd, path);
}

long implus_wl_send(int fd, const void *buf, unsigned long len) {
    return implus_unix_send(fd, buf, len);
}

long implus_wl_recv(int fd, void *buf, unsigned long len) {
    return implus_unix_recv(fd, buf, len);
}

long implus_wl_sendmsg(int fd, void *msg) {
    return implus_unix_sendmsg(fd, msg);
}

long implus_wl_recvmsg(int fd, void *msg) {
    return implus_unix_recvmsg(fd, msg);
}

int implus_wl_close(int fd) {
    return implus_unix_close(fd);
}
