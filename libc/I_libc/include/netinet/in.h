#pragma once

#include <sys/socket.h>
#include <sys/types.h>

struct in_addr {
    in_addr_t s_addr;
};

struct in6_addr {
    unsigned char s6_addr[16];
};

struct sockaddr_in {
    sa_family_t    sin_family;
    in_port_t      sin_port;
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};

#define INADDR_ANY       0x00000000u
#define INADDR_LOOPBACK  0x7f000001u
#define IN_MULTICAST(a)  ((((uint32_t)(a)) & 0xf0000000u) == 0xe0000000u)

#define IPPROTO_IP   0
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

uint16_t htons(uint16_t hostshort);
uint16_t ntohs(uint16_t netshort);
uint32_t htonl(uint32_t hostlong);
uint32_t ntohl(uint32_t netlong);
