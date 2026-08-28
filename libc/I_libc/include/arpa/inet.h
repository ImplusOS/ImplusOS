#pragma once

#include <netinet/in.h>

int inet_aton(const char* cp, struct in_addr* inp);
in_addr_t inet_addr(const char* cp);
char *inet_ntoa(struct in_addr in);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int inet_pton(int af, const char *src, void *dst);
