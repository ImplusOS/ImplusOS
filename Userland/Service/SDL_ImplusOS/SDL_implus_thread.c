#include "SDL_internal.h"

#include "thread/SDL_systhread.h"

#include <pthread.h>

static void *implus_sdl_thread_entry(void *data)
{
    SDL_RunThread((SDL_Thread *)data);
    return NULL;
}

bool SDL_SYS_CreateThread(SDL_Thread *thread,
                          SDL_FunctionPointer pfnBeginThread,
                          SDL_FunctionPointer pfnEndThread)
{
    (void)pfnBeginThread;
    (void)pfnEndThread;

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        return SDL_SetError("pthread_attr_init failed");
    }
    (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    if (thread->stacksize != 0) {
        (void)pthread_attr_setstacksize(&attr, thread->stacksize);
    }

    if (pthread_create(&thread->handle, &attr, implus_sdl_thread_entry, thread) != 0) {
        (void)pthread_attr_destroy(&attr);
        return SDL_SetError("pthread_create failed");
    }

    (void)pthread_attr_destroy(&attr);
    thread->threadid = (SDL_ThreadID)thread->handle;
    return true;
}

void SDL_SYS_SetupThread(const char *name)
{
    (void)name;
}

SDL_ThreadID SDL_GetCurrentThreadID(void)
{
    return (SDL_ThreadID)pthread_self();
}

bool SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority)
{
    (void)priority;
    return true;
}

void SDL_SYS_WaitThread(SDL_Thread *thread)
{
    if (thread) {
        (void)pthread_join(thread->handle, NULL);
    }
}

void SDL_SYS_DetachThread(SDL_Thread *thread)
{
    if (thread) {
        (void)pthread_detach(thread->handle);
    }
}
