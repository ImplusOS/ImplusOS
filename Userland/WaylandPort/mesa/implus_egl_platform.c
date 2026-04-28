#include "implus_drm.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLConfig;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean;
typedef void* EGLNativeDisplayType;
typedef void* EGLNativeWindowType;

#define EGL_TRUE  1
#define EGL_FALSE 0
#define EGL_SUCCESS 0x3000
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY  ((EGLDisplay)0)
#define EGL_NO_SURFACE  ((EGLSurface)0)
#define EGL_NO_CONTEXT  ((EGLContext)0)

typedef struct {
    int drm_fd;
    uint32_t width, height;
    uint32_t fb_handle;
    uint32_t fb_pitch;
    uint64_t fb_size;
    uint32_t fb_id;
    void *fb_map;
    uint32_t *backbuffer;
} implus_egl_display_t;

typedef struct {
    implus_egl_display_t *dpy;
    uint32_t width, height;
} implus_egl_surface_t;

typedef struct {
    implus_egl_display_t *dpy;
} implus_egl_context_t;

static implus_egl_display_t g_display;
static implus_egl_surface_t g_surface;
static implus_egl_context_t g_context;
static int g_initialized = 0;

EGLDisplay eglGetDisplay(EGLNativeDisplayType native_display) {
    (void)native_display;
    return (EGLDisplay)&g_display;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    implus_egl_display_t *d = (implus_egl_display_t*)dpy;
    if (g_initialized) { if(major) *major=1; if(minor) *minor=4; return EGL_TRUE; }

    d->drm_fd = implus_drm_open();
    if (d->drm_fd < 0) return EGL_FALSE;

    d->width = 1024; d->height = 768;

    if (drmModeCreateDumbBuffer(d->drm_fd, d->width, d->height, 32,
                                &d->fb_handle, &d->fb_pitch, &d->fb_size) != 0)
        return EGL_FALSE;

    uint64_t offset = 0;
    if (drmModeMapDumbBuffer(d->drm_fd, d->fb_handle, &offset) != 0)
        return EGL_FALSE;

    d->fb_map = implus_drm_mmap(d->drm_fd, offset, d->fb_size);

    if (drmModeAddFB(d->drm_fd, d->width, d->height, 24, 32,
                     d->fb_pitch, d->fb_handle, &d->fb_id) != 0)
        return EGL_FALSE;

    d->backbuffer = (uint32_t*)d->fb_map;
    g_initialized = 1;
    if (major) *major = 1;
    if (minor) *minor = 4;
    return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size, EGLint *num_config) {
    (void)dpy; (void)attrib_list;
    static int dummy_config;
    if (configs && config_size > 0) configs[0] = &dummy_config;
    if (num_config) *num_config = 1;
    return EGL_TRUE;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context, const EGLint *attrib_list) {
    (void)config; (void)share_context; (void)attrib_list;
    g_context.dpy = (implus_egl_display_t*)dpy;
    return (EGLContext)&g_context;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                   EGLNativeWindowType win, const EGLint *attrib_list) {
    (void)config; (void)win; (void)attrib_list;
    implus_egl_display_t *d = (implus_egl_display_t*)dpy;
    g_surface.dpy = d;
    g_surface.width = d->width;
    g_surface.height = d->height;
    return (EGLSurface)&g_surface;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    (void)dpy; (void)draw; (void)read; (void)ctx;
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    implus_egl_display_t *d = (implus_egl_display_t*)dpy;
    (void)surface;
    drmModePageFlip(d->drm_fd, 1, d->fb_id, 0, NULL);
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
    implus_egl_display_t *d = (implus_egl_display_t*)dpy;
    implus_drm_close(d->drm_fd);
    g_initialized = 0;
    return EGL_TRUE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    (void)dpy; (void)interval;
    return EGL_TRUE;
}

void *eglGetProcAddress(const char *procname) {
    (void)procname;
    return NULL;
}

extern int drmModeCreateDumbBuffer(int, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*, uint64_t*);
extern int drmModeMapDumbBuffer(int, uint32_t, uint64_t*);
extern int drmModeAddFB(int, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
extern int drmModePageFlip(int, uint32_t, uint32_t, uint32_t, void*);
