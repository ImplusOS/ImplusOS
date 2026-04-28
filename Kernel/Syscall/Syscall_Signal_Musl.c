#include "Syscall_Main.h"
#include "../ProcessManager/ProcessManager.h"
#include "../Sync/Spinlock.h"
#include "../KernelConfig.h"

#include <stddef.h>
#include <stdint.h>

#define MUSL_SIG_DFL     0
#define MUSL_SIG_IGN     1
#define MUSL_SA_RESTART  0x10000000

typedef struct {
    uint64_t sa_handler;
    uint64_t sa_flags;
    uint64_t sa_restorer;
    uint64_t sa_mask[2];
} kernel_sigaction_t;

#define SIG_TABLE_SIZE OS_CONFIG_PROCESS_MAX_COUNT
#define SIG_MAX        64

typedef struct {
    uint8_t            used;
    kernel_sigaction_t actions[SIG_MAX];
    uint64_t           blocked_mask[2];
} process_signal_state_t;

static process_signal_state_t g_sig_states[SIG_TABLE_SIZE];
static spinlock_t             g_sig_lock;
static int                    g_sig_initialized = 0;

static void sig_ensure_init(void)
{
    if (!g_sig_initialized) {
        spinlock_init(&g_sig_lock);
        for (int i = 0; i < SIG_TABLE_SIZE; ++i) {
            g_sig_states[i].used = 0;
        }
        g_sig_initialized = 1;
    }
}

static process_signal_state_t *sig_get_state(int32_t pid)
{
    if (pid < 0 || pid >= SIG_TABLE_SIZE) return NULL;
    if (!g_sig_states[pid].used) {
        g_sig_states[pid].used = 1;
        for (int i = 0; i < SIG_MAX; ++i) {
            g_sig_states[pid].actions[i].sa_handler = MUSL_SIG_DFL;
            g_sig_states[pid].actions[i].sa_flags   = 0;
            g_sig_states[pid].actions[i].sa_restorer = 0;
            g_sig_states[pid].actions[i].sa_mask[0]  = 0;
            g_sig_states[pid].actions[i].sa_mask[1]  = 0;
        }
        g_sig_states[pid].blocked_mask[0] = 0;
        g_sig_states[pid].blocked_mask[1] = 0;
    }
    return &g_sig_states[pid];
}

int64_t syscall_rt_sigaction(uint64_t signum, uint64_t act_ptr,
                             uint64_t oldact_ptr, uint64_t sigsetsize)
{
    sig_ensure_init();
    (void)sigsetsize;

    if (signum == 0 || signum >= SIG_MAX) {
        return -22;
    }

    if (signum == 9 || signum == 19) {
        return -22;
    }

    int32_t pid = current_pid_get();
    spinlock_lock(&g_sig_lock);
    process_signal_state_t *state = sig_get_state(pid);
    if (!state) {
        spinlock_unlock(&g_sig_lock);
        return -22;
    }

    if (oldact_ptr != 0) {
        kernel_sigaction_t *oldact = (kernel_sigaction_t *)(uintptr_t)oldact_ptr;
        if (process_user_buffer_is_valid(oldact, sizeof(*oldact))) {
            *oldact = state->actions[signum];
        }
    }

    if (act_ptr != 0) {
        const kernel_sigaction_t *act = (const kernel_sigaction_t *)(uintptr_t)act_ptr;
        if (process_user_buffer_is_valid((const void *)act, sizeof(*act))) {
            state->actions[signum] = *act;
            process_signal_set_handler((int32_t)signum, act->sa_handler);
        }
    }

    spinlock_unlock(&g_sig_lock);
    return 0;
}

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

int64_t syscall_rt_sigprocmask(uint64_t how, uint64_t set_ptr,
                               uint64_t oldset_ptr, uint64_t sigsetsize)
{
    sig_ensure_init();
    (void)sigsetsize;

    int32_t pid = current_pid_get();
    spinlock_lock(&g_sig_lock);
    process_signal_state_t *state = sig_get_state(pid);
    if (!state) {
        spinlock_unlock(&g_sig_lock);
        return -22;
    }

    if (oldset_ptr != 0) {
        uint64_t *oldset = (uint64_t *)(uintptr_t)oldset_ptr;
        if (process_user_buffer_is_valid(oldset, sizeof(uint64_t) * 2)) {
            oldset[0] = state->blocked_mask[0];
            oldset[1] = state->blocked_mask[1];
        }
    }

    if (set_ptr != 0) {
        const uint64_t *set = (const uint64_t *)(uintptr_t)set_ptr;
        if (process_user_buffer_is_valid(set, sizeof(uint64_t) * 2)) {
            switch ((int)how) {
                case SIG_BLOCK:
                    state->blocked_mask[0] |= set[0];
                    state->blocked_mask[1] |= set[1];
                    break;
                case SIG_UNBLOCK:
                    state->blocked_mask[0] &= ~set[0];
                    state->blocked_mask[1] &= ~set[1];
                    break;
                case SIG_SETMASK:
                    state->blocked_mask[0] = set[0];
                    state->blocked_mask[1] = set[1];
                    break;
                default:
                    spinlock_unlock(&g_sig_lock);
                    return -22;
            }
        }
    }

    spinlock_unlock(&g_sig_lock);
    return 0;
}

int64_t syscall_rt_sigreturn(void)
{
    return 0;
}

int64_t syscall_sigaltstack(uint64_t ss_ptr, uint64_t old_ss_ptr)
{
    (void)ss_ptr;
    (void)old_ss_ptr;
    return 0;
}

int64_t syscall_tkill(int32_t tid, int32_t sig)
{
    (void)tid;
    (void)sig;
    return 0;
}

void syscall_signal_cleanup_process(int32_t pid)
{
    sig_ensure_init();
    if (pid < 0 || pid >= SIG_TABLE_SIZE) return;
    spinlock_lock(&g_sig_lock);
    g_sig_states[pid].used = 0;
    spinlock_unlock(&g_sig_lock);
}
