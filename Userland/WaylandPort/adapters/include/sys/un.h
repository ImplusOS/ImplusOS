#pragma once
#define AF_UNIX 1
#define AF_LOCAL AF_UNIX
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_CLOEXEC 0x80000
#define SOL_SOCKET 1
#define SO_PASSCRED 16
#define SCM_RIGHTS 1
#define SCM_CREDENTIALS 2

struct sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
};

struct ucred {
    int pid;
    int uid;
    int gid;
};

struct msghdr {
    void *msg_name;
    unsigned int msg_namelen;
    struct iovec_compat *msg_iov;
    unsigned long msg_iovlen;
    void *msg_control;
    unsigned long msg_controllen;
    int msg_flags;
};

struct cmsghdr {
    unsigned long cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

#define CMSG_ALIGN(len) (((len)+sizeof(long)-1) & ~(sizeof(long)-1))
#define CMSG_DATA(cmsg) ((unsigned char*)((struct cmsghdr*)(cmsg)+1))
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr))+CMSG_ALIGN(len))
#define CMSG_LEN(len)   (CMSG_ALIGN(sizeof(struct cmsghdr))+(len))
#define CMSG_FIRSTHDR(mhdr) \
    ((mhdr)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr*)(mhdr)->msg_control : (struct cmsghdr*)0)
#define CMSG_NXTHDR(mhdr, cmsg) \
    (((unsigned long)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len) + sizeof(struct cmsghdr)) > \
     ((unsigned long)(mhdr)->msg_control + (mhdr)->msg_controllen) ? \
     (struct cmsghdr*)0 : (struct cmsghdr*)((unsigned long)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len)))
