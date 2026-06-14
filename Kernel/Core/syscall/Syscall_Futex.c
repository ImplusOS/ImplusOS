#include "Syscall_Main.h"
#include "Core/process/ProcessManager.h"
#include "Core/usercopy/Usercopy.h"
#include "Core/timer/Timer.h"
#include "Core/sync/Spinlock.h"

#include <stddef.h>
#include <stdint.h>

#define FUTEX_WAIT_QUEUE_SIZE  128

#define FUTEX_WAIT        0
#define FUTEX_WAKE        1
#define FUTEX_WAIT_BITSET 9
#define FUTEX_PRIVATE     128
#define FUTEX_CMD_MASK    0x7f

typedef struct {
    uint8_t   used;
    int32_t   tid;
    int32_t   owner_pid;
    uint64_t  uaddr;
    uint32_t  bitset;
    uint64_t  deadline_ms;
} futex_waiter_t;

static futex_waiter_t g_futex_waiters[FUTEX_WAIT_QUEUE_SIZE];
static spinlock_t     g_futex_lock;
static int            g_futex_initialized = 0;

static void futex_ensure_init(void)
{
    if (!g_futex_initialized) {
        spinlock_init(&g_futex_lock);
        for (int i = 0; i < FUTEX_WAIT_QUEUE_SIZE; ++i) {
            g_futex_waiters[i].used = 0;
        }
        g_futex_initialized = 1;
    }
}

static uint64_t futex_uptime_ms(void)
{
    uint32_t hz = timer_hz();
    if (hz == 0) hz = 60;
    return (timer_ticks() * 1000ULL) / hz;
}

int64_t syscall_futex_wait(uint64_t uaddr, int32_t expected,
                           uint64_t timeout_ns, uint32_t bitset)
{
    futex_ensure_init();

    if (uaddr == 0) {
        return -14;
    }
    
    int32_t *ptr = (int32_t *)(uintptr_t)uaddr;
    if (!process_user_buffer_is_valid(ptr, sizeof(int32_t))) {
        return -14;
    }

    int32_t current_value = 0;
    if (copy_from_user(&current_value, ptr, sizeof(current_value)) != 0u) {
        return -14;
    }

    if (current_value != expected) {
        return -11;
    }

    int32_t tid = process_get_current_tid();
    int32_t owner_pid = process_get_current_pid();
    if (tid < 0 || owner_pid < 0) {
        return -3;
    }

    uint64_t deadline = 0;
    if (timeout_ns > 0) {
        uint64_t timeout_ms = (timeout_ns + 999999ULL) / 1000000ULL;
        deadline = futex_uptime_ms() + timeout_ms;
    }

    spinlock_lock(&g_futex_lock);
    current_value = 0;
    if (copy_from_user(&current_value, ptr, sizeof(current_value)) != 0u) {
        spinlock_unlock(&g_futex_lock);
        return -14;
    }
    if (current_value != expected) {
        spinlock_unlock(&g_futex_lock);
        return -11;
    }
    int slot = -1;
    for (int i = 0; i < FUTEX_WAIT_QUEUE_SIZE; ++i) {
        if (!g_futex_waiters[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        spinlock_unlock(&g_futex_lock);
        return -12;
    }
    g_futex_waiters[slot].used        = 1;
    g_futex_waiters[slot].tid         = tid;
    g_futex_waiters[slot].owner_pid   = owner_pid;
    g_futex_waiters[slot].uaddr       = uaddr;
    g_futex_waiters[slot].bitset      = bitset ? bitset : 0xFFFFFFFFu;
    g_futex_waiters[slot].deadline_ms = deadline;
    if (process_block_current() < 0) {
        g_futex_waiters[slot].used = 0;
        spinlock_unlock(&g_futex_lock);
        return -3;
    }
    spinlock_unlock(&g_futex_lock);
    return 0;
}

int64_t syscall_futex_wake(uint64_t uaddr, int32_t count, uint32_t bitset)
{
    futex_ensure_init();

    if (count <= 0) {
        return 0;
    }
    if (bitset == 0) {
        bitset = 0xFFFFFFFFu;
    }
    if (!process_user_buffer_is_valid((const void *)(uintptr_t)uaddr,
                                      sizeof(int32_t))) {
        return -14;
    }

    int32_t owner_pid = process_get_current_pid();
    int32_t tids[FUTEX_WAIT_QUEUE_SIZE];
    int32_t woken = 0;
    spinlock_lock(&g_futex_lock);
    for (int i = 0; i < FUTEX_WAIT_QUEUE_SIZE && woken < count; ++i) {
        if (g_futex_waiters[i].used &&
            g_futex_waiters[i].owner_pid == owner_pid &&
            g_futex_waiters[i].uaddr == uaddr &&
            (g_futex_waiters[i].bitset & bitset) != 0) {
            tids[woken] = g_futex_waiters[i].tid;
            g_futex_waiters[i].used = 0;
            ++woken;
        }
    }
    spinlock_unlock(&g_futex_lock);
    for (int32_t i = 0; i < woken; ++i) {
        (void)process_wake_pid(tids[i]);
    }
    return (int64_t)woken;
}

void syscall_futex_on_timer_tick(void)
{
    if (!g_futex_initialized) {
        return;
    }

    uint64_t now = futex_uptime_ms();
    int32_t tids[FUTEX_WAIT_QUEUE_SIZE];
    int32_t count = 0;

    spinlock_lock(&g_futex_lock);
    for (int i = 0; i < FUTEX_WAIT_QUEUE_SIZE; ++i) {
        if (g_futex_waiters[i].used &&
            g_futex_waiters[i].deadline_ms != 0 &&
            g_futex_waiters[i].deadline_ms <= now) {
            tids[count++] = g_futex_waiters[i].tid;
            g_futex_waiters[i].used = 0;
        }
    }
    spinlock_unlock(&g_futex_lock);

    for (int32_t i = 0; i < count; ++i) {
        (void)process_wake_pid(tids[i]);
    }
}

int64_t syscall_futex(uint64_t uaddr, uint64_t op, uint64_t val,
                      uint64_t timeout_or_val2, uint64_t uaddr2,
                      uint64_t val3)
{
    (void)uaddr2;

    int cmd = (int)(op & FUTEX_CMD_MASK);

    switch (cmd) {
        case FUTEX_WAIT:
            return syscall_futex_wait(uaddr, (int32_t)val,
                                      timeout_or_val2, 0xFFFFFFFFu);
        case FUTEX_WAKE:
            return syscall_futex_wake(uaddr, (int32_t)val, 0xFFFFFFFFu);
        case FUTEX_WAIT_BITSET:
            return syscall_futex_wait(uaddr, (int32_t)val,
                                      timeout_or_val2, (uint32_t)val3);
        default:
            return -38;
    }
}
