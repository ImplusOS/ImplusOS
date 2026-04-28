#pragma once

#include <stdint.h>

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

int32_t socket_create(int32_t type);
int32_t socket_connect(int32_t sockfd, uint32_t ip, uint16_t port);
int32_t socket_bind(int32_t sockfd, uint16_t port);
int32_t socket_listen(int32_t sockfd);
int32_t socket_accept(int32_t sockfd);
int32_t socket_send(int32_t sockfd, const void *data, uint32_t len);
int32_t socket_recv(int32_t sockfd, void *buf, uint32_t buf_len);
int32_t socket_close(int32_t sockfd);
