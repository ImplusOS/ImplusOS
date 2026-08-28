#include "UnixSocket.h"
#include "Core/sync/Spinlock.h"
#include <stddef.h>
#include <string.h>

#define UNIX_SOCK_FD_MAX 16

struct _kernel_cmsghdr {
    uint32_t cmsg_len;
    uint32_t __pad1;
    int32_t cmsg_level;
    int32_t cmsg_type;
};

typedef struct {
    uint8_t used;
    uint8_t listening;
    uint8_t connected;
    int32_t peer_fd;
    char path[UNIX_SOCK_PATH_MAX];
    uint8_t buf[UNIX_SOCK_BUF_SIZE];
    uint32_t buf_head, buf_tail;
    int32_t fd_queue[UNIX_SOCK_FD_MAX];
    uint32_t fds_head, fds_tail;
    spinlock_t lock;
} unix_sock_t;

static unix_sock_t g_usocks[UNIX_SOCK_MAX];
static int g_usock_init_done = 0;

void unix_socket_init(void) {
    memset(g_usocks, 0, sizeof(g_usocks));
    for (int i = 0; i < UNIX_SOCK_MAX; i++) spinlock_init(&g_usocks[i].lock);
    g_usock_init_done = 1;
}

static unix_sock_t *usock_get(int32_t fd) {
    int idx = fd - UNIX_SOCK_FD_BASE;
    if (idx < 0 || idx >= UNIX_SOCK_MAX) return NULL;
    return g_usocks[idx].used ? &g_usocks[idx] : NULL;
}

int64_t unix_socket_create(int32_t type) {
    (void)type;
    if (!g_usock_init_done) unix_socket_init();
    for (int i = 0; i < UNIX_SOCK_MAX; i++) {
        if (!g_usocks[i].used) {
            memset(&g_usocks[i], 0, sizeof(unix_sock_t));
            spinlock_init(&g_usocks[i].lock);
            g_usocks[i].used = 1;
            g_usocks[i].peer_fd = -1;
            return UNIX_SOCK_FD_BASE + i;
        }
    }
    return -24;
}

int64_t unix_socket_bind(int32_t fd, const char *path) {
    unix_sock_t *s = usock_get(fd);
    if (!s || !path) return -14;
    size_t len = 0;
    while (path[len] && len < UNIX_SOCK_PATH_MAX - 1) len++;
    memcpy(s->path, path, len);
    s->path[len] = 0;
    return 0;
}

int64_t unix_socket_listen(int32_t fd, int32_t backlog) {
    (void)backlog;
    unix_sock_t *s = usock_get(fd);
    if (!s) return -9;
    s->listening = 1;
    return 0;
}

int64_t unix_socket_accept(int32_t fd) {
    unix_sock_t *s = usock_get(fd);
    if (!s || !s->listening) return -22;
    for (int i = 0; i < UNIX_SOCK_MAX; i++) {
        if (g_usocks[i].used && g_usocks[i].connected && g_usocks[i].peer_fd == fd) {
            int64_t new_fd = unix_socket_create(0);
            if (new_fd < 0) return new_fd;
            unix_sock_t *ns = usock_get((int32_t)new_fd);
            if (ns) { ns->connected = 1; ns->peer_fd = UNIX_SOCK_FD_BASE + i; g_usocks[i].peer_fd = (int32_t)new_fd; }
            return new_fd;
        }
    }
    return -11;
}

int64_t unix_socket_connect(int32_t fd, const char *path) {
    unix_sock_t *s = usock_get(fd);
    if (!s || !path) return -14;
    for (int i = 0; i < UNIX_SOCK_MAX; i++) {
        if (g_usocks[i].used && g_usocks[i].listening) {
            size_t plen = 0; while (g_usocks[i].path[plen]) plen++;
            size_t clen = 0; while (path[clen]) clen++;
            if (plen == clen && memcmp(g_usocks[i].path, path, plen) == 0) {
                s->connected = 1;
                s->peer_fd = UNIX_SOCK_FD_BASE + i;
                return 0;
            }
        }
    }
    return -111;
}

int64_t unix_socket_send(int32_t fd, const void *buf, uint64_t len) {
    unix_sock_t *s = usock_get(fd);
    if (!s || !s->connected || !buf) return -14;
    unix_sock_t *peer = usock_get(s->peer_fd);
    if (!peer) return -32;
    spinlock_lock(&peer->lock);
    uint64_t written = 0;
    const uint8_t *src = (const uint8_t*)buf;
    while (written < len) {
        uint32_t next = (peer->buf_head + 1) % UNIX_SOCK_BUF_SIZE;
        if (next == peer->buf_tail) break;
        peer->buf[peer->buf_head] = src[written++];
        peer->buf_head = next;
    }
    spinlock_unlock(&peer->lock);
    return (int64_t)written;
}

int64_t unix_socket_recv(int32_t fd, void *buf, uint64_t len) {
    unix_sock_t *s = usock_get(fd);
    if (!s || !buf) return -14;
    spinlock_lock(&s->lock);
    uint64_t rd = 0;
    uint8_t *dst = (uint8_t*)buf;
    while (rd < len && s->buf_tail != s->buf_head) {
        dst[rd++] = s->buf[s->buf_tail];
        s->buf_tail = (s->buf_tail + 1) % UNIX_SOCK_BUF_SIZE;
    }
    spinlock_unlock(&s->lock);
    return (int64_t)rd;
}

int64_t unix_socket_sendmsg(int32_t fd, uint64_t msg_ptr) {
    /* Layout matches glibc's struct msghdr exactly (all of msg_namelen,
     * msg_iovlen, msg_controllen, msg_flags are padded/sized per the
     * real ABI - msg_iovlen/msg_controllen are 8-byte size_t, NOT
     * uint32_t, on x86-64; getting this wrong silently misreads every
     * field after it). */
    struct { uint64_t name; uint32_t namelen; uint32_t __pad0; uint64_t iov;
             uint64_t iovlen; uint64_t control; uint64_t controllen;
             int32_t flags; uint32_t __pad1; } *msg = (void*)(uintptr_t)msg_ptr;
    if (!msg) return -14;
    
    if (msg->control && msg->controllen > 0) {
        unix_sock_t *s = usock_get(fd);
        if (s && s->connected) {
            unix_sock_t *peer = usock_get(s->peer_fd);
            if (peer) {
                spinlock_lock(&peer->lock);
                uint8_t *cdata = (uint8_t*)(uintptr_t)msg->control;
                uint32_t offset = 0;
                while (offset + sizeof(struct _kernel_cmsghdr) <= msg->controllen) {
                    struct _kernel_cmsghdr *cmsg = (struct _kernel_cmsghdr *)(cdata + offset);
                    if (cmsg->cmsg_len < sizeof(struct _kernel_cmsghdr) || offset + cmsg->cmsg_len > msg->controllen) break;
                    if (cmsg->cmsg_level == 1 && cmsg->cmsg_type == 1) {
                        int32_t *fds = (int32_t *)(cdata + offset + sizeof(struct _kernel_cmsghdr));
                        int num_fds = (cmsg->cmsg_len - sizeof(struct _kernel_cmsghdr)) / sizeof(int32_t);
                        for (int i = 0; i < num_fds; i++) {
                            uint32_t next = (peer->fds_head + 1) % UNIX_SOCK_FD_MAX;
                            if (next != peer->fds_tail) {
                                peer->fd_queue[peer->fds_head] = fds[i];
                                peer->fds_head = next;
                            }
                        }
                    }
                    offset += (cmsg->cmsg_len + 7) & ~7ULL;
                }
                spinlock_unlock(&peer->lock);
            }
        }
    }

    struct { uint64_t base; uint64_t len; } *iov = (void*)(uintptr_t)msg->iov;
    if (!iov || msg->iovlen == 0) return 0;
    int64_t total = 0;
    for (uint32_t i = 0; i < msg->iovlen; i++) {
        int64_t r = unix_socket_send(fd, (void*)(uintptr_t)iov[i].base, iov[i].len);
        if (r < 0) return total > 0 ? total : r;
        total += r;
    }
    return total;
}

int64_t unix_socket_recvmsg(int32_t fd, uint64_t msg_ptr) {
    /* Layout matches glibc's struct msghdr exactly (all of msg_namelen,
     * msg_iovlen, msg_controllen, msg_flags are padded/sized per the
     * real ABI - msg_iovlen/msg_controllen are 8-byte size_t, NOT
     * uint32_t, on x86-64; getting this wrong silently misreads every
     * field after it). */
    struct { uint64_t name; uint32_t namelen; uint32_t __pad0; uint64_t iov;
             uint64_t iovlen; uint64_t control; uint64_t controllen;
             int32_t flags; uint32_t __pad1; } *msg = (void*)(uintptr_t)msg_ptr;
    if (!msg) return -14;
    struct { uint64_t base; uint64_t len; } *iov = (void*)(uintptr_t)msg->iov;
    int64_t total = 0;
    if (iov && msg->iovlen > 0) {
        for (uint32_t i = 0; i < msg->iovlen; i++) {
            int64_t r = unix_socket_recv(fd, (void*)(uintptr_t)iov[i].base, iov[i].len);
            if (r < 0) return total > 0 ? total : r;
            total += r;
            if (r < iov[i].len) break;
        }
    }
    
    unix_sock_t *s = usock_get(fd);
    if (s && msg->control && msg->controllen >= sizeof(struct _kernel_cmsghdr)) {
        spinlock_lock(&s->lock);
        int num_fds = 0;
        uint32_t temp_tail = s->fds_tail;
        while (temp_tail != s->fds_head) {
            num_fds++;
            temp_tail = (temp_tail + 1) % UNIX_SOCK_FD_MAX;
        }
        
        if (num_fds > 0) {
            uint32_t avail_space = msg->controllen - sizeof(struct _kernel_cmsghdr);
            int write_fds = num_fds;
            if (write_fds * sizeof(int32_t) > avail_space) {
                write_fds = avail_space / sizeof(int32_t);
            }
            
            struct _kernel_cmsghdr *cmsg = (struct _kernel_cmsghdr *)(uintptr_t)msg->control;
            cmsg->cmsg_len = sizeof(struct _kernel_cmsghdr) + write_fds * sizeof(int32_t);
            cmsg->__pad1 = 0;
            cmsg->cmsg_level = 1;
            cmsg->cmsg_type = 1;
            
            int32_t *fds = (int32_t *)((uint8_t *)(uintptr_t)msg->control + sizeof(struct _kernel_cmsghdr));
            for (int i = 0; i < write_fds; i++) {
                fds[i] = s->fd_queue[s->fds_tail];
                s->fds_tail = (s->fds_tail + 1) % UNIX_SOCK_FD_MAX;
            }
            msg->controllen = cmsg->cmsg_len;
        } else {
            msg->controllen = 0;
        }
        spinlock_unlock(&s->lock);
    } else {
        msg->controllen = 0;
    }
    
    return total;
}

int64_t unix_socket_close(int32_t fd) {
    unix_sock_t *s = usock_get(fd);
    if (!s) return -9;
    s->used = 0;
    return 0;
}

int64_t unix_socket_pair(int32_t out_fds[2]) {
    if (!out_fds) return -14;
    int64_t fd1 = unix_socket_create(0);
    if (fd1 < 0) return fd1;
    int64_t fd2 = unix_socket_create(0);
    if (fd2 < 0) {
        (void)unix_socket_close((int32_t)fd1);
        return fd2;
    }
    unix_sock_t *s1 = usock_get((int32_t)fd1);
    unix_sock_t *s2 = usock_get((int32_t)fd2);
    if (!s1 || !s2) {
        (void)unix_socket_close((int32_t)fd1);
        (void)unix_socket_close((int32_t)fd2);
        return -22;
    }
    s1->connected = 1;
    s1->peer_fd = (int32_t)fd2;
    s2->connected = 1;
    s2->peer_fd = (int32_t)fd1;
    out_fds[0] = (int32_t)fd1;
    out_fds[1] = (int32_t)fd2;
    return 0;
}

int unix_socket_fd_in_range(int32_t fd) {
    return fd >= UNIX_SOCK_FD_BASE && fd < UNIX_SOCK_FD_BASE + UNIX_SOCK_MAX;
}
