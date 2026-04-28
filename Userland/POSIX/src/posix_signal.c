 

#include "../include/posix_signal.h"
#include "../include/posix_process.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

 

typedef void (*os_signal_handler_t)(int32_t signum);
extern os_signal_handler_t signal(int32_t signum, os_signal_handler_t handler);
extern void sleep_ms(uint64_t ms);

 

static struct sigaction g_sigact[NSIG];      
static sigset_t         g_signal_mask = 0;   
static sigset_t         g_pending     = 0;   

 

 
static void signal_trampoline(int signum)
{
    if (signum < 1 || signum >= NSIG) {
        return;
    }

     
    if (g_signal_mask & (1ULL << (signum - 1))) {
        g_pending |= (1ULL << (signum - 1));
        return;
    }

    struct sigaction *sa = &g_sigact[signum];

    if (sa->sa_handler == SIG_IGN) {
        return;
    }
    if (sa->sa_handler == SIG_DFL) {
         
        if (signum != SIGCHLD && signum != SIGURG) {
            posix_exit(128 + signum);
        }
        return;
    }

     
    sigset_t saved = g_signal_mask;
    g_signal_mask |= sa->sa_mask;
    if (!(sa->sa_flags & SA_NODEFER)) {
        g_signal_mask |= (1ULL << (signum - 1));
    }

     
    if (sa->sa_flags & SA_RESETHAND) {
        sighandler_t old = sa->sa_handler;
        sa->sa_handler = SIG_DFL;
        signal((int32_t)signum, (os_signal_handler_t)signal_trampoline);
        old(signum);
    } else {
        sa->sa_handler(signum);
    }

    g_signal_mask = saved;
}

 

sighandler_t posix_signal(int signum, sighandler_t handler)
{
    if (signum < 1 || signum >= NSIG) {
        errno = EINVAL;
        return SIG_ERR;
    }

    sighandler_t prev = g_sigact[signum].sa_handler;

    g_sigact[signum].sa_handler = handler;
    sigemptyset(&g_sigact[signum].sa_mask);
    g_sigact[signum].sa_flags = 0;

     
    os_signal_handler_t ret = signal((int32_t)signum,
                                     (os_signal_handler_t)signal_trampoline);
    if ((uintptr_t)ret == (uintptr_t)SIG_ERR) {
        errno = EINVAL;
        return SIG_ERR;
    }

    os_errno = 0;
    return prev ? prev : SIG_DFL;
}

 

int posix_sigaction(int signum, const struct sigaction *act,
                    struct sigaction *oldact)
{
    if (signum < 1 || signum >= NSIG) {
        errno = EINVAL;
        return -1;
    }
     
    if (signum == SIGKILL || signum == SIGSTOP) {
        errno = EINVAL;
        return -1;
    }

    if (oldact) {
        memcpy(oldact, &g_sigact[signum], sizeof(struct sigaction));
    }

    if (act) {
        memcpy(&g_sigact[signum], act, sizeof(struct sigaction));
         
        if (act->sa_handler != SIG_DFL && act->sa_handler != SIG_IGN) {
            signal((int32_t)signum, (os_signal_handler_t)signal_trampoline);
        } else if (act->sa_handler == SIG_IGN) {
            signal((int32_t)signum, (os_signal_handler_t)signal_trampoline);
        } else {
             
            signal((int32_t)signum, (os_signal_handler_t)signal_trampoline);
        }
    }

    os_errno = 0;
    return 0;
}

 

int posix_sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    if (oldset) {
        *oldset = g_signal_mask;
    }

    if (!set) {
        os_errno = 0;
        return 0;
    }

    switch (how) {
        case SIG_BLOCK:
            g_signal_mask |= *set;
            break;
        case SIG_UNBLOCK:
            g_signal_mask &= ~(*set);
             
            {
                sigset_t deliverable = g_pending & ~g_signal_mask;
                for (int s = 1; s < NSIG; s++) {
                    if (deliverable & (1ULL << (s - 1))) {
                        g_pending &= ~(1ULL << (s - 1));
                        signal_trampoline(s);
                    }
                }
            }
            break;
        case SIG_SETMASK:
            g_signal_mask = *set;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    os_errno = 0;
    return 0;
}

 

int posix_sigpending(sigset_t *set)
{
    if (!set) {
        errno = EINVAL;
        return -1;
    }
    *set = g_pending;
    os_errno = 0;
    return 0;
}

 

int posix_raise(int sig)
{
    if (sig < 1 || sig >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    signal_trampoline(sig);
    os_errno = 0;
    return 0;
}
