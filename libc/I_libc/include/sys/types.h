#pragma once

#include <stddef.h>
#include <stdint.h>

typedef long ssize_t;
typedef long off_t;
typedef int pid_t;
typedef unsigned int mode_t;
typedef unsigned long nfds_t;
typedef unsigned int socklen_t;
typedef uint16_t sa_family_t;
typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;
typedef long suseconds_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int nlink_t;
typedef long long quad_t;
typedef unsigned long long u_quad_t;

struct iovec {
    void  *iov_base;
    size_t iov_len;
};
