#pragma once

#include <netinet/in.h>

int inet_aton(const char* cp, struct in_addr* inp);
in_addr_t inet_addr(const char* cp);
