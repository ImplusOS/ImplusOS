#pragma once
#include <stdint.h>

#define UNIX_SOCK_FD_BASE 0x8000
#define UNIX_SOCK_MAX 8
#define UNIX_SOCK_BUF_SIZE 1024
#define UNIX_SOCK_PATH_MAX 108
#define SCM_RIGHTS 1
#define AF_UNIX 1
#define SOCK_STREAM_UNIX 1

void unix_socket_init(void);
int64_t unix_socket_create(int32_t type);
int64_t unix_socket_bind(int32_t fd, const char *path);
int64_t unix_socket_listen(int32_t fd, int32_t backlog);
int64_t unix_socket_accept(int32_t fd);
int64_t unix_socket_connect(int32_t fd, const char *path);
int64_t unix_socket_send(int32_t fd, const void *buf, uint64_t len);
int64_t unix_socket_recv(int32_t fd, void *buf, uint64_t len);
int64_t unix_socket_sendmsg(int32_t fd, uint64_t msg_ptr);
int64_t unix_socket_recvmsg(int32_t fd, uint64_t msg_ptr);
int64_t unix_socket_close(int32_t fd);
/* socketpair(AF_UNIX, ...) - creates two already-connected endpoints
 * without bind()/listen()/connect()/accept(). Writes the two new fds to
 * out_fds[0]/out_fds[1] and returns 0, or a negative error. */
int64_t unix_socket_pair(int32_t out_fds[2]);
/* True if `fd` falls in the Unix-domain-socket fd range (regardless of
 * whether it is currently in use) - lets callers route by fd alone. */
int unix_socket_fd_in_range(int32_t fd);
