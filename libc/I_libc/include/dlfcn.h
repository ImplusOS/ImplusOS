#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define RTLD_LAZY    0x00001
#define RTLD_NOW     0x00002
#define RTLD_BINDING_MASK 0x3
#define RTLD_NOLOAD  0x00004
#define RTLD_DEEPBIND 0x00008
#define RTLD_GLOBAL  0x00100
#define RTLD_LOCAL   0x00000
#define RTLD_NODELETE 0x01000

#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT    ((void *)-1L)

void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *symbol);
int   dlclose(void *handle);
char *dlerror(void);

void implus_dl_thread_init(void);
void implus_dl_thread_cleanup(void);

#ifdef __cplusplus
}
#endif
