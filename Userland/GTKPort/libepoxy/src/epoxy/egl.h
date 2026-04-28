#pragma once

#include "gl.h"

typedef void *EGLDisplay;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLConfig;
typedef int    EGLint;
typedef unsigned int EGLBoolean;

#define EGL_TRUE  1
#define EGL_FALSE 0
#define EGL_NO_DISPLAY   ((EGLDisplay)0)
#define EGL_NO_SURFACE   ((EGLSurface)0)
#define EGL_NO_CONTEXT   ((EGLContext)0)

int epoxy_egl_version(EGLDisplay dpy);
int epoxy_has_egl_extension(EGLDisplay dpy, const char *extension);
