#include "implus_drm.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static int g_drm_fd = -1;

int drmOpen(const char *name, const char *busid) {
    (void)name; (void)busid;
    g_drm_fd = implus_drm_open();
    return g_drm_fd;
}

int drmClose(int fd) {
    return implus_drm_close(fd);
}

int drmIoctl(int fd, unsigned long request, void *arg) {
    return (int)implus_drm_ioctl(fd, request, arg);
}

int drmGetCap(int fd, uint64_t capability, uint64_t *value) {
    struct { uint64_t cap; uint64_t val; } c;
    c.cap = capability; c.val = 0;
    int r = (int)implus_drm_ioctl(fd, 0xC1, &c);
    if (r == 0 && value) *value = c.val;
    return r;
}

void *drmMmap(int fd, uint64_t offset, uint64_t size) {
    return implus_drm_mmap(fd, offset, size);
}

typedef struct {
    uint32_t handle;
    uint32_t width, height, bpp, pitch;
    uint64_t size;
} drmCreateDumb_t;

int drmModeCreateDumbBuffer(int fd, uint32_t w, uint32_t h, uint32_t bpp,
                            uint32_t *handle, uint32_t *pitch, uint64_t *size) {
    drmCreateDumb_t d;
    memset(&d, 0, sizeof(d));
    d.width = w; d.height = h; d.bpp = bpp;
    int r = (int)implus_drm_ioctl(fd, 0xB2, &d);
    if (r == 0) { *handle = d.handle; *pitch = d.pitch; *size = d.size; }
    return r;
}

int drmModeMapDumbBuffer(int fd, uint32_t handle, uint64_t *offset) {
    struct { uint32_t handle; uint32_t pad; uint64_t offset; } m;
    m.handle = handle; m.pad = 0; m.offset = 0;
    int r = (int)implus_drm_ioctl(fd, 0xB3, &m);
    if (r == 0) *offset = m.offset;
    return r;
}

int drmModeAddFB(int fd, uint32_t w, uint32_t h, uint32_t depth, uint32_t bpp,
                 uint32_t pitch, uint32_t handle, uint32_t *buf_id) {
    struct { uint32_t fb_id, width, height, pitch, bpp, depth, handle; } f;
    memset(&f, 0, sizeof(f));
    f.width = w; f.height = h; f.pitch = pitch; f.bpp = bpp; f.depth = depth; f.handle = handle;
    int r = (int)implus_drm_ioctl(fd, 0xAE, &f);
    if (r == 0) *buf_id = f.fb_id;
    return r;
}

int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t flags, void *user_data) {
    struct { uint32_t crtc_id, fb_id, flags, reserved; uint64_t user_data; } pf;
    pf.crtc_id = crtc_id; pf.fb_id = fb_id; pf.flags = flags; pf.reserved = 0;
    pf.user_data = (uint64_t)(uintptr_t)user_data;
    return (int)implus_drm_ioctl(fd, 0xB0, &pf);
}
