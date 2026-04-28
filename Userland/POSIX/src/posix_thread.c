 

#include "../include/posix_thread.h"
#include "../include/posix_process.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

 

extern uint64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2);
extern void     sleep_ms(uint64_t ms);
extern uint64_t get_uptime_ms(void);

#define SYSCALL_THREAD_CREATE 9ULL

 

#define POSIX_TLS_MAX_KEYS   64
#define POSIX_MAX_THREADS   128

typedef struct {
    pthread_t  tid;
    void      *slots[POSIX_TLS_MAX_KEYS];
} posix_tls_record_t;

static posix_tls_record_t g_tls_table[POSIX_MAX_THREADS];
static pthread_key_t      g_next_key = 1;
static void (*g_key_destructor[POSIX_TLS_MAX_KEYS])(void *);

static posix_tls_record_t *tls_find(pthread_t tid)
{
    for (int i = 0; i < POSIX_MAX_THREADS; i++) {
        if (g_tls_table[i].tid == tid) {
            return &g_tls_table[i];
        }
    }
    return NULL;
}

static posix_tls_record_t *tls_alloc(pthread_t tid)
{
    posix_tls_record_t *r = tls_find(tid);
    if (r) return r;
    for (int i = 0; i < POSIX_MAX_THREADS; i++) {
        if (g_tls_table[i].tid == 0) {
            g_tls_table[i].tid = tid;
            return &g_tls_table[i];
        }
    }
    return NULL;
}

 

 
static void thread_entry(posix_thread_desc_t *desc)
{
    if (!desc) {
        return;
    }

     
    pthread_t self = (pthread_t)(uintptr_t)desc;
    tls_alloc(self);

    void *ret = desc->routine(desc->arg);
    desc->retval = ret;
    __sync_synchronize();
    desc->done = 1;

    if (desc->detached) {
        free(desc);
    }
     
}

 

int posix_pthread_create(pthread_t *thread,
                         const pthread_attr_t *attr,
                         void *(*start_routine)(void *),
                         void *arg)
{
    if (!thread || !start_routine) {
        return EINVAL;
    }

    posix_thread_desc_t *desc =
        (posix_thread_desc_t *)malloc(sizeof(posix_thread_desc_t));
    if (!desc) {
        return ENOMEM;
    }

    memset(desc, 0, sizeof(*desc));
    desc->routine      = start_routine;
    desc->arg          = arg;
    desc->done         = 0;
    desc->retval       = NULL;
    desc->detached     = (attr && attr->detached) ?
                          PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
    desc->cancel_state = PTHREAD_CANCEL_ENABLE;

    uint64_t r = syscall2(SYSCALL_THREAD_CREATE,
                           (uint64_t)(uintptr_t)thread_entry,
                           (uint64_t)(uintptr_t)desc);
    if ((int64_t)r < 0) {
        free(desc);
        return EAGAIN;
    }

    *thread = (pthread_t)(uintptr_t)desc;
    return 0;
}

 

int posix_pthread_join(pthread_t thread, void **retval)
{
    if (!thread) {
        return EINVAL;
    }
    posix_thread_desc_t *desc = (posix_thread_desc_t *)(uintptr_t)thread;

    while (!desc->done) {
        sleep_ms(1);
    }
    __sync_synchronize();

    if (retval) {
        *retval = desc->retval;
    }
    if (!desc->detached) {
        free(desc);
    }
    return 0;
}

 

int posix_pthread_detach(pthread_t thread)
{
    if (!thread) {
        return EINVAL;
    }
    posix_thread_desc_t *desc = (posix_thread_desc_t *)(uintptr_t)thread;
    desc->detached = PTHREAD_CREATE_DETACHED;
    return 0;
}

 

pthread_t posix_pthread_self(void)
{
     
    return (pthread_t)(uintptr_t)posix_getpid();
}

 

int posix_pthread_equal(pthread_t a, pthread_t b)
{
    return a == b;
}

 

int posix_pthread_cancel(pthread_t thread)
{
    if (!thread) {
        return EINVAL;
    }
    posix_thread_desc_t *desc = (posix_thread_desc_t *)(uintptr_t)thread;
    if (desc->cancel_state == PTHREAD_CANCEL_DISABLE) {
        return 0;       
    }
     
    desc->retval = (void *)((intptr_t)-1);  
    __sync_synchronize();
    desc->done   = 1;
    return 0;
}

 

int posix_pthread_setcancelstate(int state, int *oldstate)
{
     
    static int s_cancel_state = PTHREAD_CANCEL_ENABLE;
    if (oldstate) {
        *oldstate = s_cancel_state;
    }
    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE) {
        return EINVAL;
    }
    s_cancel_state = state;
    return 0;
}

 

int posix_pthread_once(pthread_once_t *once_ctrl, void (*init_routine)(void))
{
    if (!once_ctrl || !init_routine) {
        return EINVAL;
    }
    if (__sync_bool_compare_and_swap(&once_ctrl->done, 0, 1)) {
        init_routine();
    }
    return 0;
}

 

int posix_pthread_mutex_init(pthread_mutex_t *mutex,
                              const pthread_mutexattr_t *attr)
{
    if (!mutex) return EINVAL;
    mutex->locked = 0;
    mutex->type   = attr ? attr->type : PTHREAD_MUTEX_NORMAL;
    mutex->owner  = 0;
    return 0;
}

int posix_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;
    mutex->locked = 0;
    mutex->owner  = 0;
    return 0;
}

int posix_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;
    pthread_t self = posix_pthread_self();

    if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        if (mutex->locked && mutex->owner == self) {
            mutex->locked++;
            return 0;
        }
    } else if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
        if (mutex->locked && mutex->owner == self) {
            return EBUSY;
        }
    }

    while (__sync_lock_test_and_set(&mutex->locked, 1)) {
        sleep_ms(1);
    }
    mutex->owner = self;
    return 0;
}

int posix_pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;
    pthread_t self = posix_pthread_self();

    if (__sync_lock_test_and_set(&mutex->locked, 1)) {
        return EBUSY;
    }
    mutex->owner = self;
    return 0;
}

int posix_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->locked > 1) {
        mutex->locked--;
        return 0;
    }
    mutex->owner = 0;
    __sync_lock_release(&mutex->locked);
    return 0;
}

int posix_pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    if (!attr) return EINVAL;
    attr->pshared = 0;
    attr->type    = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int posix_pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    if (!attr) return EINVAL;
    attr->pshared = 0;
    attr->type    = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int posix_pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    if (!attr) return EINVAL;
    attr->type = type;
    return 0;
}

 

int posix_pthread_cond_init(pthread_cond_t *cond,
                             const pthread_condattr_t *attr)
{
    (void)attr;
    if (!cond) return EINVAL;
    cond->seq = 0;
    return 0;
}

int posix_pthread_cond_destroy(pthread_cond_t *cond)
{
    if (!cond) return EINVAL;
    cond->seq = 0;
    return 0;
}

int posix_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    if (!cond || !mutex) return EINVAL;
    unsigned snap = cond->seq;
    posix_pthread_mutex_unlock(mutex);
    while (cond->seq == snap) {
        sleep_ms(1);
    }
    posix_pthread_mutex_lock(mutex);
    return 0;
}

int posix_pthread_cond_timedwait(pthread_cond_t *cond,
                                  pthread_mutex_t *mutex,
                                  const struct timespec *abstime)
{
    if (!cond || !mutex || !abstime) return EINVAL;
    unsigned snap = cond->seq;
    uint64_t deadline =
        (uint64_t)abstime->tv_sec * 1000ULL +
        (uint64_t)(abstime->tv_nsec / 1000000L);

    posix_pthread_mutex_unlock(mutex);
    while (cond->seq == snap) {
        if (get_uptime_ms() >= deadline) {
            posix_pthread_mutex_lock(mutex);
            return ETIMEDOUT;
        }
        sleep_ms(1);
    }
    posix_pthread_mutex_lock(mutex);
    return 0;
}

int posix_pthread_cond_signal(pthread_cond_t *cond)
{
    if (!cond) return EINVAL;
    __sync_add_and_fetch(&cond->seq, 1);
    return 0;
}

int posix_pthread_cond_broadcast(pthread_cond_t *cond)
{
    return posix_pthread_cond_signal(cond);
}

int posix_pthread_condattr_init(pthread_condattr_t *attr)
{
    if (!attr) return EINVAL;
    attr->pshared = 0;
    return 0;
}

int posix_pthread_condattr_destroy(pthread_condattr_t *attr)
{
    if (!attr) return EINVAL;
    attr->pshared = 0;
    return 0;
}

 

int posix_pthread_key_create(pthread_key_t *key,
                              void (*destructor)(void *))
{
    if (!key) return EINVAL;
    if (g_next_key >= POSIX_TLS_MAX_KEYS) return EAGAIN;
    pthread_key_t k       = g_next_key++;
    g_key_destructor[k]   = destructor;
    *key = k;
    return 0;
}

int posix_pthread_key_delete(pthread_key_t key)
{
    if (key == 0 || key >= POSIX_TLS_MAX_KEYS) return EINVAL;
    g_key_destructor[key] = NULL;
    return 0;
}

void *posix_pthread_getspecific(pthread_key_t key)
{
    if (key == 0 || key >= POSIX_TLS_MAX_KEYS) return NULL;
    pthread_t self = posix_pthread_self();
    posix_tls_record_t *r = tls_find(self);
    if (!r) return NULL;
    return r->slots[key];
}

int posix_pthread_setspecific(pthread_key_t key, const void *value)
{
    if (key == 0 || key >= POSIX_TLS_MAX_KEYS) return EINVAL;
    pthread_t self = posix_pthread_self();
    posix_tls_record_t *r = tls_alloc(self);
    if (!r) return ENOMEM;
    r->slots[key] = (void *)value;
    return 0;
}
