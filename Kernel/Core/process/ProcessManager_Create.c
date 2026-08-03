#include "ProcessManager.h"
#include "ProcessScheduler.h"

#include "IPC/IPC_Main.h"
#include "IPC/PnP_Notifications.h"
#include "Core/elf/ELF_Loader.h"
#include "Core/memory/SharedMemory.h"
#include "cpu/GDT_Main.h"
#include "MemoryManagement/Memory_Main.h"
#include "mmu/Paging_Main.h"
#include "Core/sync/Spinlock.h"
#include "Core/syscall/Syscall_File.h"
#include "Core/syscall/Syscall_Socket.h"
#include "Core/syscall/Syscall_Main.h"
#include "Core/syscall/Syscall_Futex.h"
#include "Core/usercopy/Usercopy.h"
#include "Core/timer/Timer.h"
#include "interfaces/hal_cpu.h"
#include "interfaces/arch_ops.h"
#include "smp/SMP_Main.h"
#include <string.h>
#include "Debug/serial/Serial.h"
#include "Debug/printf/printf.h"
#if defined(__aarch64__)
#include "cpu/Exception.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define PROCESS_RFLAGS_DEFAULT 0x202ULL
#define PROCESS_GUARD_PAGE_SIZE PAGE_SIZE
#define PROCESS_INITIAL_USER_STACK_SIZE (16ULL * PAGE_SIZE)
#define PROCESS_THREAD_STACK_SIZE (1024ULL * 1024ULL)
#define PROCESS_THREAD_STACK_REGION_SIZE \
    (PROCESS_THREAD_STACK_SIZE + PROCESS_GUARD_PAGE_SIZE)

#if defined(__aarch64__)
#define PROCESS_CONTEXT_QWORDS ((uint32_t)(sizeof(arm64_exception_frame_t) / sizeof(uint64_t)))
#else
#define PROCESS_CONTEXT_QWORDS SYSCALL_FRAME_QWORDS
#endif
#define PROCESS_ELF_MAX_SIZE (20ULL * 1024ULL * 1024ULL)

#define IA32_FS_BASE      0xC0000100U
#define IA32_KERNEL_GS_BASE 0xC0000102U

static inline uint64_t rdmsr_fs_base(void)
{
#if defined(__aarch64__)
    uint64_t val;
    __asm__ volatile("mrs %0, TPIDR_EL0" : "=r"(val));
    return val;
#else
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(IA32_FS_BASE));
    return ((uint64_t)hi << 32) | lo;
#endif
}

static inline void wrmsr_fs_base(uint64_t val)
{
#if defined(__aarch64__)
    __asm__ volatile("msr TPIDR_EL0, %0" :: "r"(val) : "memory");
#else
    uint32_t lo = (uint32_t)(val & 0xFFFFFFFFU);
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" :: "c"(IA32_FS_BASE), "a"(lo), "d"(hi) : "memory");
#endif
}

static inline void wrmsr_gs_base(uint64_t val)
{
    hal_cpu_write_gs_base(val);
}

static inline uint64_t rdmsr_kernel_gs_base(void)
{
#if defined(__aarch64__)
    return 0;
#else
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(IA32_KERNEL_GS_BASE));
    return ((uint64_t)hi << 32) | lo;
#endif
}

static inline void wrmsr_kernel_gs_base(uint64_t val)
{
#if defined(__aarch64__)
    (void)val;
#else
    uint32_t lo = (uint32_t)(val & 0xFFFFFFFFU);
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" :: "c"(IA32_KERNEL_GS_BASE), "a"(lo), "d"(hi) : "memory");
#endif
}

static inline void process_cpu_halt(void)
{
#if defined(__aarch64__)
    __asm__ volatile("wfi");
#else
    __asm__ volatile("hlt");
#endif
}

static inline void process_fpu_save(uint8_t *state)
{
    hal_cpu_save_fpu(state);
}

static inline void process_fpu_restore(uint8_t *state)
{
    hal_cpu_restore_fpu(state);
}

typedef struct {
    uint64_t a_type;
    uint64_t a_val;
} process_auxv_t;

enum {
    PROCESS_AT_NULL   = 0,
    PROCESS_AT_PHDR   = 3,
    PROCESS_AT_PHENT  = 4,
    PROCESS_AT_PHNUM  = 5,
    PROCESS_AT_PAGESZ = 6,
    PROCESS_AT_ENTRY  = 9,
    PROCESS_AT_UID    = 11,
    PROCESS_AT_EUID   = 12,
    PROCESS_AT_GID    = 13,
    PROCESS_AT_EGID   = 14,
    PROCESS_AT_SECURE = 23,
    PROCESS_AT_RANDOM = 25,
    PROCESS_AT_EXECFN = 31,
};

static process_t *g_processes = NULL;
static int32_t g_process_capacity = 0;
static uint64_t *g_sleep_deadline_ns = NULL;
static spinlock_t g_process_table_lock;
static uint32_t g_timeslice_ticks = 4;
static uint64_t g_cpu_usage_prev_runtime_ns[OS_CONFIG_PROCESS_MAX_COUNT_MAX];
static uint64_t g_cpu_usage_prev_syscalls[OS_CONFIG_PROCESS_MAX_COUNT_MAX];
static uint64_t g_cpu_usage_prev_display_bytes[OS_CONFIG_PROCESS_MAX_COUNT_MAX];

static void initialize_fpu_state(uint8_t *fpu_state)
{
    if (fpu_state == NULL) {
        return;
    }

    memset(fpu_state, 0, PROCESS_FPU_STATE_SIZE);
    fpu_state[0] = 0x7F;
    fpu_state[1] = 0x03;
    fpu_state[24] = 0x80;
    fpu_state[25] = 0x1F;
}

int32_t current_pid_get(void)
{
    return process_scheduler_current_pid();
}

static inline void current_pid_set(int32_t pid)
{
    process_scheduler_set_current_pid(pid);
}


static void halt_forever(void)
{
    hal_cpu_disable_interrupts();
    while (1) {
        process_cpu_halt();
    }
}

void process_broadcast_shutdown(void)
{
    uint32_t signal = IPC_SIGNAL_SHUTDOWN;
    for (int32_t i = 0; i < g_process_capacity; ++i) {
        if (g_processes[i].state == PROCESS_STATE_RUNNING ||
            g_processes[i].state == PROCESS_STATE_READY ||
            g_processes[i].state == PROCESS_STATE_BLOCKED) {
            if (i != current_pid_get()) {
                ipc_send_message(i, &signal, sizeof(uint32_t));
            }
        }
    }

    
    for (int retry = 0; retry < 100; retry++) {
        int all_dead = 1;
        for (int32_t i = 0; i < g_process_capacity; ++i) {
            if (i == current_pid_get()) continue;
            if (g_processes[i].state != PROCESS_STATE_UNUSED &&
                g_processes[i].state != PROCESS_STATE_DEAD) {
                all_dead = 0;
                break;
            }
        }
        if (all_dead) break;
        
        
        process_cpu_halt();
    }
}

static int align_up_u64_checked(uint64_t value, uint64_t align, uint64_t *result_out)
{
    if (result_out == NULL || align == 0 || (align & (align - 1ULL)) != 0) {
        return -1;
    }

    uint64_t addend = align - 1ULL;
    if (value > (UINT64_MAX - addend)) {
        return -1;
    }

    *result_out = (value + addend) & ~addend;
    return 0;
}

static int is_valid_user_entry(uint64_t entry)
{
    return (entry >= 0x1000) &&
           (entry < USER_CODE_LIMIT);
}

static int process_table_ready(void)
{
    return g_processes != NULL && g_process_capacity > 0;
}

static void process_clear_sleep_deadline_for_proc_locked(const process_t *proc)
{
    if (g_sleep_deadline_ns == NULL || g_processes == NULL ||
        proc == NULL || g_process_capacity <= 0) {
        return;
    }

    uintptr_t base = (uintptr_t)g_processes;
    uintptr_t end = base + (uintptr_t)g_process_capacity * sizeof(process_t);
    uintptr_t value = (uintptr_t)proc;
    if (value < base || value >= end) {
        return;
    }

    uintptr_t offset = value - base;
    if ((offset % sizeof(process_t)) != 0u) {
        return;
    }

    size_t index = (size_t)(offset / sizeof(process_t));
    g_sleep_deadline_ns[index] = 0u;
}

static uint64_t process_deadline_after_ns(uint64_t now_ns, uint64_t delay_ns)
{
    if (UINT64_MAX - now_ns < delay_ns) {
        return UINT64_MAX;
    }
    return now_ns + delay_ns;
}

static uint64_t process_perf_now_ns(void)
{
    return timer_monotonic_ns();
}

static void process_perf_reset(process_t *proc)
{
    if (proc == NULL) {
        return;
    }
    proc->runtime_ns = 0;
    proc->ready_wait_ns = 0;
    proc->max_ready_wait_ns = 0;
    proc->context_switches = 0;
    proc->voluntary_switches = 0;
    proc->involuntary_switches = 0;
    proc->syscalls = 0;
    proc->ipc_send = 0;
    proc->ipc_recv = 0;
    proc->block_count = 0;
    proc->wake_count = 0;
    proc->blocked_ns = 0;
    proc->display_present_calls = 0;
    proc->display_rects = 0;
    proc->display_bytes = 0;
    proc->last_scheduled_ns = 0;
    proc->ready_since_ns = 0;
    proc->blocked_since_ns = 0;
}

static void process_perf_account_runtime_locked(process_t *proc, uint64_t now_ns)
{
    if (proc == NULL || proc->last_scheduled_ns == 0u) {
        return;
    }
    if (now_ns >= proc->last_scheduled_ns) {
        proc->runtime_ns += now_ns - proc->last_scheduled_ns;
    }
    proc->last_scheduled_ns = 0u;
}

static void process_perf_mark_ready_locked(process_t *proc, uint64_t now_ns)
{
    if (proc == NULL) {
        return;
    }
    if (proc->ready_since_ns == 0u) {
        proc->ready_since_ns = now_ns;
    }
}

static void process_perf_prepare_run_locked(process_t *proc,
                                            uint64_t now_ns,
                                            int involuntary)
{
    if (proc == NULL) {
        return;
    }
    if (proc->ready_since_ns != 0u && now_ns >= proc->ready_since_ns) {
        uint64_t wait_ns = now_ns - proc->ready_since_ns;
        proc->ready_wait_ns += wait_ns;
        if (wait_ns > proc->max_ready_wait_ns) {
            proc->max_ready_wait_ns = wait_ns;
        }
    }
    proc->ready_since_ns = 0u;
    proc->last_scheduled_ns = now_ns;
    proc->context_switches++;
    if (involuntary) {
        proc->involuntary_switches++;
    } else {
        proc->voluntary_switches++;
    }
}

static void process_wake_sleepers_locked(uint64_t now_ns)
{
    if (g_sleep_deadline_ns == NULL || !process_table_ready()) {
        return;
    }

    for (int32_t i = 0; i < g_process_capacity; ++i) {
        uint64_t deadline_ns = g_sleep_deadline_ns[i];
        if (deadline_ns == 0u) {
            continue;
        }

        process_t *proc = &g_processes[i];
        if (proc->state != PROCESS_STATE_BLOCKED) {
            g_sleep_deadline_ns[i] = 0u;
            continue;
        }
        if (now_ns < deadline_ns) {
            continue;
        }

        g_sleep_deadline_ns[i] = 0u;
        if (proc->blocked_since_ns != 0u &&
            now_ns >= proc->blocked_since_ns) {
            proc->blocked_ns += now_ns - proc->blocked_since_ns;
        }
        proc->blocked_since_ns = 0u;
        proc->wake_count++;
        proc->state = PROCESS_STATE_READY;
        process_perf_mark_ready_locked(proc, now_ns);
        process_scheduler_request_reschedule();
    }
}

static int is_valid_pid(int32_t pid)
{
    return process_table_ready() && pid >= 0 && pid < g_process_capacity;
}

static void save_syscall_frame_to_process(process_t *proc, uint64_t current_saved_rsp)
{
    if (proc == NULL || current_saved_rsp == 0) return;
    proc->saved_rsp = current_saved_rsp;
    proc->gs_base = hal_cpu_read_gs_base();
}

static void reset_process_slot(process_t *proc)
{
    if (proc == NULL) {
        return;
    }

    process_clear_sleep_deadline_for_proc_locked(proc);

    proc->state = PROCESS_STATE_UNUSED;
    proc->is_thread = 0;
    proc->thread_detached = 0;
    proc->capability_mask = 0;
    proc->entry = 0;
    proc->saved_rsp = 0;
    proc->saved_user_rsp = 0;

    initialize_fpu_state(proc->fpu_state);

    proc->fs_base = 0;
    proc->gs_base = 0;

    proc->cr3 = 0;
    proc->kernel_stack_base = NULL;
    proc->kernel_stack_top = 0;
    proc->user_code_base = 0;
    proc->user_code_limit = 0;
    proc->user_heap_base = 0;
    proc->user_heap_cursor = 0;
    proc->user_heap_limit = 0;
    proc->user_heap_alloc_limit = 0;
    proc->user_heap_guard_page = 0;
    proc->user_stack_base = 0;
    proc->user_stack_top = 0;
    proc->user_stack_guard_page = 0;
    proc->timeslice = 0;
    proc->total_ticks = 0;
    process_perf_reset(proc);
    memset(proc->name, 0, sizeof(proc->name));
    memset(proc->cwd, 0, sizeof(proc->cwd));
    proc->cwd[0] = '/';
    memset(proc->launch_argument, 0, sizeof(proc->launch_argument));
    proc->parent_pid = -1;
    proc->memory_owner_pid = -1;
    proc->exit_status = 0;
    proc->thread_stack_region_base = 0;
    proc->thread_stack_region_size = 0;
    for (uint32_t i = 0; i < PROCESS_USER_ALLOC_MAX; ++i) {
        proc->user_allocs[i].used = 0;
        proc->user_allocs[i].addr = 0;
        proc->user_allocs[i].size = 0;
    }
    for (uint32_t i = 0; i < PROCESS_SIGNAL_MAX; ++i) {
        proc->signal_handlers[i] = 0;
    }
    proc->signal_mask = 0;
    proc->pending_signals = 0;
    proc->clear_child_tid = 0;
    proc->robust_list_head = 0;
    proc->robust_list_length = 0;
    proc->rseq_area = 0;
    proc->rseq_sig = 0;
}

static void release_thread_resources(process_t *proc, int unmap_user_stack)
{
    if (proc == NULL || !proc->is_thread) {
        return;
    }

    if (unmap_user_stack && proc->cr3 != 0 &&
        proc->thread_stack_region_base != 0 &&
        proc->thread_stack_region_size != 0) {
        (void)paging_unmap_range(proc->cr3,
                                 proc->thread_stack_region_base,
                                 proc->thread_stack_region_size);
    }
    if (proc->kernel_stack_base != NULL) {
        free(proc->kernel_stack_base);
        proc->kernel_stack_base = NULL;
    }
    proc->cr3 = 0;
    proc->thread_stack_region_base = 0;
    proc->thread_stack_region_size = 0;
    proc->kernel_stack_top = 0;
}

static void release_process_resources(process_t *proc)
{
    if (proc == NULL) {
        return;
    }

    if (proc->is_thread) {
        release_thread_resources(proc, 1);
        return;
    }

    int32_t owner_pid = -1;
    if (g_processes != NULL && proc >= g_processes &&
        proc < (g_processes + g_process_capacity)) {
        owner_pid = (int32_t)(proc - g_processes);
    }

    if (owner_pid >= 0) {
        for (int32_t i = 1; i < g_process_capacity; ++i) {
            process_t *thread = &g_processes[i];
            if (thread != proc && thread->is_thread &&
                thread->memory_owner_pid == owner_pid &&
                thread->state != PROCESS_STATE_UNUSED) {
                if (process_scheduler_pid_in_use_on_any_cpu(i)) {
                    continue;
                }
                release_thread_resources(thread, 0);
                reset_process_slot(thread);
            }
        }
    }

    if (proc->cr3 != 0) {
        paging_destroy_process_space(proc->cr3);
        proc->cr3 = 0;
    }
    if (proc->kernel_stack_base != NULL) {
        free(proc->kernel_stack_base);
        proc->kernel_stack_base = NULL;
    }
    proc->kernel_stack_top = 0;
    proc->capability_mask = 0;
    proc->user_code_base = 0;
    proc->user_code_limit = 0;
    proc->user_heap_base = 0;
    proc->user_heap_cursor = 0;
    proc->user_heap_limit = 0;
    proc->user_heap_alloc_limit = 0;
    proc->user_heap_guard_page = 0;
    proc->user_stack_base = 0;
    proc->user_stack_top = 0;
    proc->user_stack_guard_page = 0;
    for (uint32_t i = 0; i < PROCESS_USER_ALLOC_MAX; ++i) {
        proc->user_allocs[i].used = 0;
        proc->user_allocs[i].addr = 0;
        proc->user_allocs[i].size = 0;
    }
    for (uint32_t i = 0; i < PROCESS_SIGNAL_MAX; ++i) {
        proc->signal_handlers[i] = 0;
    }
    proc->signal_mask = 0;
    proc->pending_signals = 0;
    proc->clear_child_tid = 0;
    proc->robust_list_head = 0;
    proc->robust_list_length = 0;
    proc->rseq_area = 0;
    proc->rseq_sig = 0;
}

static process_t *process_memory_owner_locked(process_t *proc)
{
    if (proc == NULL) {
        return NULL;
    }
    if (!proc->is_thread) {
        return proc;
    }
    if (!is_valid_pid(proc->memory_owner_pid)) {
        return NULL;
    }
    process_t *owner = &g_processes[proc->memory_owner_pid];
    if (owner->state == PROCESS_STATE_UNUSED || owner->is_thread) {
        return NULL;
    }
    return owner;
}

static int process_push_signal_frame_locked(process_t *proc, int32_t signum)
{
    if (proc == NULL || signum <= 0 || signum >= PROCESS_SIGNAL_MAX) {
        return -1;
    }

    uint64_t handler = proc->signal_handlers[(uint32_t)signum];
    if (handler == 1u) {
        proc->pending_signals &= ~(1u << (uint32_t)signum);
        return 0;
    }
    if (handler == 0) {
        proc->exit_status = 128 + signum;
        proc->state = PROCESS_STATE_ZOMBIE;
        return -1;
    }

#if defined(__x86_64__)
    uint64_t *frame = (uint64_t *)(uintptr_t)proc->saved_rsp;
    if (frame == NULL || proc->saved_user_rsp < (USER_STACK_BASE + 16ULL)) {
        proc->exit_status = 128 + signum;
        proc->state = PROCESS_STATE_ZOMBIE;
        return -1;
    }

    uint64_t old_rip = frame[SYSCALL_FRAME_RCX];
    uint64_t new_user_rsp = (proc->saved_user_rsp - 8ULL) & ~0xFULL;
    new_user_rsp -= 8ULL;
    if (new_user_rsp < proc->user_stack_base || new_user_rsp >= proc->user_stack_top) {
        proc->exit_status = 128 + signum;
        proc->state = PROCESS_STATE_ZOMBIE;
        return -1;
    }

    uint64_t old_cr3 = paging_get_active_cr3();
    paging_switch_cr3(proc->cr3);
    *(uint64_t *)(uintptr_t)new_user_rsp = old_rip;
    paging_switch_cr3(old_cr3);

    frame[SYSCALL_FRAME_RCX] = handler;
    frame[SYSCALL_FRAME_RDI] = (uint64_t)(uint32_t)signum;
    proc->saved_user_rsp = new_user_rsp;
    proc->pending_signals &= ~(1u << (uint32_t)signum);
    return 0;
#else
    proc->exit_status = 128 + signum;
    proc->state = PROCESS_STATE_ZOMBIE;
    return -1;
#endif
}

static void process_deliver_pending_signals_locked(process_t *proc)
{
    if (proc == NULL || proc->pending_signals == 0) {
        return;
    }

    for (uint32_t signum = 1; signum < PROCESS_SIGNAL_MAX; ++signum) {
        uint32_t bit = 1u << signum;
        if ((proc->pending_signals & bit) == 0u) {
            continue;
        }
        if ((proc->signal_mask & (1ULL << (signum - 1u))) != 0u &&
            signum != 9u && signum != 19u) {
            continue;
        }
        if (process_push_signal_frame_locked(proc, (int32_t)signum) < 0) {
            proc->pending_signals &= ~bit;
        }
        break;
    }
}

static void release_process_table(void)
{
    if (!process_table_ready()) {
        if (g_sleep_deadline_ns != NULL) {
            free(g_sleep_deadline_ns);
            g_sleep_deadline_ns = NULL;
        }
        return;
    }

    for (int32_t i = 0; i < g_process_capacity; ++i) {
        if (g_processes[i].state != PROCESS_STATE_UNUSED) {
            release_process_resources(&g_processes[i]);
            reset_process_slot(&g_processes[i]);
        }
    }

    free(g_processes);
    g_processes = NULL;
    free(g_sleep_deadline_ns);
    g_sleep_deadline_ns = NULL;
    g_process_capacity = 0;
    process_scheduler_init(g_timeslice_ticks);
}

static int32_t find_free_slot(void)
{
    if (!process_table_ready()) {
        return -1;
    }

    for (int32_t i = 1; i < g_process_capacity; ++i) {
        if (g_processes[i].state == PROCESS_STATE_UNUSED) {
            return i;
        }
        if (g_processes[i].state == PROCESS_STATE_DEAD &&
            !process_scheduler_pid_in_use_on_any_cpu(i)) {
            release_process_resources(&g_processes[i]);
            reset_process_slot(&g_processes[i]);
            return i;
        }
    }

    for (int32_t i = 0; i < g_process_capacity; ++i) {
        if (g_processes[i].state == PROCESS_STATE_ZOMBIE &&
            !process_scheduler_pid_in_use_on_any_cpu(i)) {
            int32_t pp = g_processes[i].parent_pid;
            if (pp < 0 || !is_valid_pid(pp) || g_processes[pp].state == PROCESS_STATE_UNUSED || g_processes[pp].state == PROCESS_STATE_DEAD) {
                release_process_resources(&g_processes[i]);
                reset_process_slot(&g_processes[i]);
                return i;
            }
        }
    }
    return -1;
}

static void activate_process_context(process_t *proc)
{
    paging_switch_cr3(proc->cr3);
    syscall_set_kernel_rsp(proc->kernel_stack_top);
    syscall_set_user_rsp(proc->saved_user_rsp);
    gdt_set_kernel_rsp0(proc->kernel_stack_top);

    wrmsr_fs_base(proc->fs_base);
    wrmsr_kernel_gs_base(proc->gs_base);
}

static void mark_process_runnable(process_t *proc, uint64_t entry, int32_t parent_pid)
{
    if (proc == NULL) {
        return;
    }

    uint64_t now_ns = process_perf_now_ns();
    proc->entry = entry;
    proc->parent_pid = parent_pid;
    proc->timeslice = g_timeslice_ticks;
    proc->state = PROCESS_STATE_READY;
    process_perf_mark_ready_locked(proc, now_ns);
    process_scheduler_request_reschedule();
}

static int initialize_raw_user_stack(process_t *proc)
{
    if (proc == NULL || proc->cr3 == 0 || proc->user_stack_top <= proc->user_stack_base) {
        return -1;
    }

    uint64_t user_rsp = proc->user_stack_top - sizeof(uint64_t);
    uint64_t old_cr3 = paging_get_active_cr3();
    paging_switch_cr3(proc->cr3);
    *(uint64_t *)(uintptr_t)user_rsp = 0;
    paging_switch_cr3(old_cr3);

    proc->saved_user_rsp = user_rsp;
    return 0;
}

#define EXECVE_ARG_MAX 256
#define EXECVE_STRTOTAL_MAX 8192

static int count_user_string_array(const char *const *user_array,
                                    uint64_t *count_out,
                                    uint64_t *total_strlen_out)
{
    *count_out = 0;
    *total_strlen_out = 0;
    if (!user_array) return 0;

    for (uint64_t i = 0; i < EXECVE_ARG_MAX; ++i) {
        if (!process_user_buffer_is_valid(&user_array[i], sizeof(const char *)))
            return -1;
        const char *str_ptr = NULL;
        if (copy_from_user(&str_ptr, &user_array[i], sizeof(const char *)) != sizeof(const char *))
            return -1;
        if (!str_ptr) return 0;
        uint64_t len = 0;
        if (process_user_cstring_length(str_ptr, EXECVE_STRTOTAL_MAX, &len) < 0)
            return -1;
        *total_strlen_out += len + 1;
        if (*total_strlen_out > EXECVE_STRTOTAL_MAX) return -1;
        (*count_out)++;
    }
    return -1;
}

static int copy_user_strings(const char *const *user_array,
                              uint64_t count,
                              char *buf, uint64_t buf_size,
                              uint64_t *offsets)
{
    uint64_t pos = 0;
    for (uint64_t i = 0; i < count; ++i) {
        const char *str_ptr = NULL;
        if (copy_from_user(&str_ptr, &user_array[i], sizeof(const char *)) != sizeof(const char *))
            return -1;
        if (!str_ptr) return -1;
        uint64_t len = 0;
        if (process_user_cstring_length(str_ptr, EXECVE_STRTOTAL_MAX, &len) < 0)
            return -1;
        if (pos + len + 1 > buf_size) return -1;
        if (copy_from_user(buf + pos, str_ptr, len + 1) != len + 1)
            return -1;
        offsets[i] = pos;
        pos += len + 1;
    }
    return 0;
}

static int initialize_elf_user_stack_ex(
    process_t *proc,
    const elf_loaded_image_info_t *image_info,
    const char *exec_path,
    uint64_t argc, const char *const *argv_kernel,
    uint64_t envc, const char *const *envp_kernel);

static int initialize_elf_user_stack(process_t *proc,
                                     const elf_loaded_image_info_t *image_info,
                                     const char *exec_path)
{
    return initialize_elf_user_stack_ex(proc, image_info, exec_path,
                                        0, NULL, 0, NULL);
}

static int initialize_elf_user_stack_ex(
    process_t *proc,
    const elf_loaded_image_info_t *image_info,
    const char *exec_path,
    uint64_t argc, const char *const *argv_kernel,
    uint64_t envc, const char *const *envp_kernel)
{
    if (proc == NULL || image_info == NULL || exec_path == NULL || proc->cr3 == 0) {
        return -1;
    }

    uint64_t old_cr3 = paging_get_active_cr3();
    uint64_t sp = proc->user_stack_top;
    uint64_t random_seed = image_info->entry ^ proc->cr3 ^ proc->kernel_stack_top;
    size_t exec_path_len = 0;
    while (exec_path[exec_path_len] != '\0') {
        ++exec_path_len;
    }
    ++exec_path_len;

    uint8_t random_bytes[16];
    for (uint32_t i = 0; i < sizeof(random_bytes); ++i) {
        random_seed = (random_seed * 6364136223846793005ULL) + 1442695040888963407ULL;
        random_bytes[i] = (uint8_t)(random_seed >> 56);
    }

    paging_switch_cr3(proc->cr3);

    sp -= (uint64_t)exec_path_len;
    memcpy((void *)(uintptr_t)sp, exec_path, exec_path_len);
    uint64_t execfn_addr = sp;

    sp &= ~0xFULL;
    sp -= sizeof(random_bytes);
    memcpy((void *)(uintptr_t)sp, random_bytes, sizeof(random_bytes));
    uint64_t random_addr = sp;

    uint64_t total_argv_strlen = 0;
    for (uint64_t i = 0; i < argc; ++i) {
        total_argv_strlen += (argv_kernel ? strlen(argv_kernel[i]) : 0) + 1;
    }
    uint64_t total_envp_strlen = 0;
    for (uint64_t i = 0; i < envc; ++i) {
        total_envp_strlen += (envp_kernel ? strlen(envp_kernel[i]) : 0) + 1;
    }

    sp &= ~0xFULL;
    sp -= total_envp_strlen;
    uint64_t envp_strings_base = sp;
    for (uint64_t i = 0; i < envc; ++i) {
        uint64_t len = strlen(envp_kernel[i]) + 1;
        memcpy((void *)(uintptr_t)sp, envp_kernel[i], len);
        sp += len;
    }
    sp = envp_strings_base;

    sp &= ~0xFULL;
    sp -= total_argv_strlen;
    uint64_t argv_strings_base = sp;
    for (uint64_t i = 0; i < argc; ++i) {
        uint64_t len = strlen(argv_kernel[i]) + 1;
        memcpy((void *)(uintptr_t)sp, argv_kernel[i], len);
        sp += len;
    }
    sp = argv_strings_base;

    process_auxv_t auxv[] = {
        { PROCESS_AT_PHDR,   image_info->phdr_vaddr },
        { PROCESS_AT_PHENT,  image_info->phent },
        { PROCESS_AT_PHNUM,  image_info->phnum },
        { PROCESS_AT_PAGESZ, PAGE_SIZE },
        { PROCESS_AT_ENTRY,  image_info->entry },
        { PROCESS_AT_UID,    0 },
        { PROCESS_AT_EUID,   0 },
        { PROCESS_AT_GID,    0 },
        { PROCESS_AT_EGID,   0 },
        { PROCESS_AT_SECURE, 0 },
        { PROCESS_AT_RANDOM, random_addr },
        { PROCESS_AT_EXECFN, execfn_addr },
        { PROCESS_AT_NULL,   0 },
    };

    uint64_t auxv_count = sizeof(auxv) / sizeof(auxv[0]);
    uint64_t auxv_words = auxv_count * 2U;

    sp &= ~0xFULL;
    
    sp -= 8ULL;
    sp -= (1ULL + argc + 1ULL + envc + 1ULL + auxv_words + 1ULL) * sizeof(uint64_t);

    uint64_t *stack_words = (uint64_t *)(uintptr_t)sp;
    uint64_t idx = 0;

    stack_words[idx++] = argc;

    uint64_t argv_base = argv_strings_base;
    for (uint64_t i = 0; i < argc; ++i) {
        stack_words[idx++] = argv_base;
        argv_base += strlen(argv_kernel[i]) + 1;
    }
    stack_words[idx++] = 0;

    uint64_t envp_base = envp_strings_base;
    for (uint64_t i = 0; i < envc; ++i) {
        stack_words[idx++] = envp_base;
        envp_base += strlen(envp_kernel[i]) + 1;
    }
    stack_words[idx++] = 0;

    for (uint64_t i = 0; i < auxv_count; ++i) {
        stack_words[idx++] = auxv[i].a_type;
        stack_words[idx++] = auxv[i].a_val;
    }
    stack_words[idx] = 0;

    paging_switch_cr3(old_cr3);
    proc->saved_user_rsp = sp;
    return 0;
}


static int initialize_process_memory(process_t *proc,
                                     uint64_t entry,
                                     uint64_t arg1,
                                     uint64_t arg2,
                                     uint64_t arg3,
                                     uint64_t arg4)
{
    if (proc == NULL) {
        return -1;
    }

    proc->kernel_stack_base = malloc(PROCESS_KERNEL_STACK_SIZE);
    if (!proc->kernel_stack_base) {
        return -1;
    }
    proc->kernel_stack_top = ((uint64_t)(uintptr_t)(proc->kernel_stack_base + PROCESS_KERNEL_STACK_SIZE)) & ~0xFULL;

    proc->cr3 = paging_create_process_space();
    if (!proc->cr3) {
        return -1;
    }

    proc->user_code_base = USER_CODE_BASE;
    proc->user_code_limit = USER_CODE_LIMIT;
    proc->user_heap_base = USER_HEAP_BASE;
    proc->user_heap_cursor = USER_HEAP_BASE;
    proc->user_heap_limit = USER_HEAP_LIMIT;
    proc->user_stack_base = USER_STACK_BASE;
    proc->user_stack_top = USER_STACK_TOP;
    proc->capability_mask = PROCESS_CAP_DEFAULT_MASK;

    if (proc->user_code_limit <= proc->user_code_base ||
        proc->user_heap_limit <= proc->user_heap_base ||
        proc->user_stack_top <= proc->user_stack_base ||
        proc->user_code_limit > proc->user_heap_base ||
        proc->user_heap_limit > proc->user_stack_base) {
        return -1;
    }

    if ((proc->user_heap_limit - proc->user_heap_base) <= PROCESS_GUARD_PAGE_SIZE ||
        (proc->user_stack_top - proc->user_stack_base) <= PROCESS_GUARD_PAGE_SIZE) {
        return -1;
    }

    proc->user_heap_guard_page = proc->user_heap_limit - PROCESS_GUARD_PAGE_SIZE;
    proc->user_heap_limit -= PROCESS_GUARD_PAGE_SIZE;
    uint64_t thread_reserve =
        (uint64_t)g_process_capacity * PROCESS_THREAD_STACK_REGION_SIZE;
    if (thread_reserve >= (proc->user_heap_limit - proc->user_heap_base)) {
        return -1;
    }
    proc->user_heap_alloc_limit = proc->user_heap_limit - thread_reserve;
    proc->user_stack_guard_page = proc->user_stack_base;
    proc->user_stack_base += PROCESS_GUARD_PAGE_SIZE;

    if (proc->user_heap_limit <= proc->user_heap_cursor ||
        proc->user_stack_top <= proc->user_stack_base ||
        proc->user_heap_limit > proc->user_stack_base) {
        return -1;
    }

    uint64_t initial_stack_base = proc->user_stack_top - PROCESS_INITIAL_USER_STACK_SIZE;
    if (initial_stack_base < proc->user_stack_base) {
        return -1;
    }

    if (paging_map_user_range_alloc(proc->cr3,
                                    initial_stack_base,
                                    PROCESS_INITIAL_USER_STACK_SIZE,
                                    PAGE_RW | PAGE_USER) < 0) {
        return -1;
    }

    initialize_fpu_state(proc->fpu_state);

    proc->fs_base = 0;
    proc->gs_base = 0;

    uint64_t *kstack = (uint64_t *)proc->kernel_stack_top;
    kstack -= PROCESS_CONTEXT_QWORDS;

    for (uint32_t i = 0; i < PROCESS_CONTEXT_QWORDS; ++i) {
        kstack[i] = 0;
    }

#if defined(__aarch64__)
    arm64_exception_frame_t *frame = (arm64_exception_frame_t *)kstack;
    frame->x[0] = arg1;
    frame->x[1] = arg2;
    frame->x[2] = arg3;
    frame->x[3] = arg4;
    frame->elr_el1 = entry;
    frame->spsr_el1 = 0;
    proc->saved_rsp = (uint64_t)(uintptr_t)frame;
#else
    kstack[SYSCALL_FRAME_RCX] = entry;
    kstack[SYSCALL_FRAME_RDI] = arg1;
    kstack[SYSCALL_FRAME_RSI] = arg2;
    kstack[SYSCALL_FRAME_RDX] = arg3;
    kstack[SYSCALL_FRAME_R8]  = arg4;
    kstack[SYSCALL_FRAME_R11] = PROCESS_RFLAGS_DEFAULT;
    proc->saved_rsp = (uint64_t)(uintptr_t)kstack;
#endif
    if (initialize_raw_user_stack(proc) < 0) {
        return -1;
    }
#if defined(__aarch64__)
    frame->sp_el0 = proc->saved_user_rsp;
#endif
    proc->timeslice = g_timeslice_ticks;
    
    return 0;
}

void *process_user_mmap(uint64_t length, uint64_t flags)
{
    (void)flags;
    if (!is_valid_pid(current_pid_get()) || length == 0) {
        return NULL;
    }

    uint64_t aligned_len = 0;
    if (align_up_u64_checked(length, PAGE_SIZE, &aligned_len) < 0) {
        return NULL;
    }

    if (aligned_len == 0 || aligned_len > UINT32_MAX) {
        return NULL;
    }

    return process_user_alloc((uint32_t)aligned_len);
}

static int range_within(uint64_t addr, uint64_t len, uint64_t start, uint64_t end)
{
    if (len == 0) {
        return 1;
    }
    if (addr > (0xFFFFFFFFFFFFFFFFULL - len)) {
        return 0;
    }
    uint64_t addr_end = addr + len;
    return (addr >= start) && (addr_end <= end);
}

void process_manager_init(void)
{
    int32_t desired_capacity = PROCESS_MAX_COUNT_CONFIG;
    if (desired_capacity < 1) {
        desired_capacity = 1;
    }

    process_scheduler_init(g_timeslice_ticks);

    release_process_table();

    uint64_t table_size_u64 = (uint64_t)desired_capacity * (uint64_t)sizeof(process_t);
    if (table_size_u64 == 0 || table_size_u64 > 0xFFFFFFFFULL) {
        halt_forever();
    }

    g_processes = (process_t *)malloc((size_t)table_size_u64);
    if (g_processes == NULL) {
        halt_forever();
    }

    uint64_t sleep_table_size_u64 =
        (uint64_t)desired_capacity * (uint64_t)sizeof(uint64_t);
    if (sleep_table_size_u64 == 0 || sleep_table_size_u64 > 0xFFFFFFFFULL) {
        halt_forever();
    }

    g_sleep_deadline_ns = (uint64_t *)malloc((size_t)sleep_table_size_u64);
    if (g_sleep_deadline_ns == NULL) {
        halt_forever();
    }
    memset(g_sleep_deadline_ns, 0, (size_t)sleep_table_size_u64);

    g_process_capacity = desired_capacity;
    for (int32_t i = 0; i < g_process_capacity; ++i) {
        reset_process_slot(&g_processes[i]);
    }
    
    g_processes[0].state = PROCESS_STATE_RUNNING;
    g_processes[0].parent_pid = -1;
    g_processes[0].memory_owner_pid = 0;
    g_processes[0].capability_mask = PROCESS_CAP_DEFAULT_MASK;
    g_processes[0].last_scheduled_ns = process_perf_now_ns();

    initialize_fpu_state(g_processes[0].fpu_state);
    g_processes[0].fs_base = 0;
    g_processes[0].gs_base = 0;
    g_processes[0].cr3 = paging_get_kernel_cr3();

    current_pid_set(0);
    spinlock_init(&g_process_table_lock);
}

void process_set_current_fs_base(uint64_t fs_base)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (is_valid_pid(current_pid_get())) {
        g_processes[current_pid_get()].fs_base = fs_base;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    wrmsr_fs_base(fs_base);
}

uint64_t process_get_current_fs_base(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    uint64_t val = 0;
    if (is_valid_pid(current_pid_get())) {
        val = g_processes[current_pid_get()].fs_base;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return val;
}

uint8_t process_get_current_abi_mode(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    uint8_t mode = PROCESS_ABI_IMPLUS;
    if (is_valid_pid(current_pid_get())) {
        mode = g_processes[current_pid_get()].abi_mode;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return mode;
}

void process_set_current_abi_mode(uint8_t mode)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (is_valid_pid(current_pid_get())) {
        g_processes[current_pid_get()].abi_mode = mode;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
}

uint64_t process_get_current_pending_signals(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    uint64_t pending = 0;
    if (is_valid_pid(current_pid_get())) {
        pending = g_processes[current_pid_get()].pending_signals;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return pending;
}

int process_consume_pending_signal(int32_t signum)
{
    if (signum <= 0 || signum >= PROCESS_SIGNAL_MAX) {
        return 0;
    }
    uint64_t bit = 1u << (uint32_t)signum;

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int consumed = 0;
    if (is_valid_pid(current_pid_get())) {
        process_t *proc = &g_processes[current_pid_get()];
        if ((proc->pending_signals & bit) != 0u) {
            proc->pending_signals &= ~bit;
            consumed = 1;
        }
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return consumed;
}

int process_set_current_name(const char *name, uint32_t max_len)
{
    if (name == NULL || max_len == 0u) {
        return -22;
    }
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int result = -22;
    if (is_valid_pid(current_pid_get())) {
        process_t *proc = &g_processes[current_pid_get()];
        uint32_t copy_len = max_len;
        if (copy_len >= sizeof(proc->name)) {
            copy_len = sizeof(proc->name) - 1u;
        }
        for (uint32_t i = 0; i < copy_len; ++i) {
            if (name[i] == '\0') {
                copy_len = i;
                break;
            }
        }
        memcpy(proc->name, name, copy_len);
        proc->name[copy_len] = '\0';
        result = 0;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return result;
}

int process_get_current_name(char *out, uint32_t capacity)
{
    if (out == NULL || capacity == 0u) {
        return -22;
    }
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int result = -22;
    if (is_valid_pid(current_pid_get())) {
        process_t *proc = &g_processes[current_pid_get()];
        uint32_t len = 0;
        while (len + 1u < capacity && proc->name[len] != '\0') {
            ++len;
        }
        memcpy(out, proc->name, len);
        out[len] = '\0';
        result = 0;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return result;
}

int process_set_current_cwd(const char *cwd)
{
    if (cwd == NULL || cwd[0] == '\0' || cwd[0] != '/') {
        return -22;
    }
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int result = -22;
    if (is_valid_pid(current_pid_get())) {
        process_t *proc = &g_processes[current_pid_get()];
        uint32_t len = 0;
        while (cwd[len] != '\0' && len + 1u < sizeof(proc->cwd)) {
            ++len;
        }
        memcpy(proc->cwd, cwd, len);
        proc->cwd[len] = '\0';
        result = 0;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return result;
}

int process_get_current_cwd(char *out, uint32_t capacity)
{
    if (out == NULL || capacity == 0u) {
        return -22;
    }
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int result = -22;
    if (is_valid_pid(current_pid_get())) {
        process_t *proc = &g_processes[current_pid_get()];
        uint32_t len = 0;
        while (len + 1u < capacity && proc->cwd[len] != '\0') {
            ++len;
        }
        memcpy(out, proc->cwd, len);
        out[len] = '\0';
        result = 0;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return result;
}

int process_set_clear_child_tid(uint64_t address)
{
    if (address != 0u &&
        !process_user_buffer_is_valid((void *)(uintptr_t)address,
                                      sizeof(uint32_t))) {
        return -14;
    }
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -3;
    }
    g_processes[current_pid_get()].clear_child_tid = address;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int process_set_robust_list(uint64_t head, uint64_t length)
{
    if (length != 24u || head == 0u ||
        !process_user_buffer_is_valid((void *)(uintptr_t)head, length)) {
        return -22;
    }
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -3;
    }
    process_t *thread = &g_processes[current_pid_get()];
    thread->robust_list_head = head;
    thread->robust_list_length = length;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int process_rseq_register(uint64_t area, uint32_t sig)
{
    if (area == 0u) {
        return -22;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -3;
    }
    process_t *proc = &g_processes[current_pid_get()];
    if (proc->rseq_area != 0u) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -16;
    }
    proc->rseq_area = area;
    proc->rseq_sig = sig;
    uint64_t cr3 = proc->cr3;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    if (cr3 != 0u &&
        paging_is_user_range_mapped(cr3, area, PROCESS_RSEQ_AREA_SIZE)) {
        uint32_t cpu = smp_get_current_cpu_id();
        memcpy((void *)(uintptr_t)(area + PROCESS_RSEQ_CPU_ID_START),
               &cpu, sizeof(cpu));
        memcpy((void *)(uintptr_t)(area + PROCESS_RSEQ_CPU_ID),
               &cpu, sizeof(cpu));
    }
    return 0;
}

int process_rseq_unregister(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -3;
    }
    g_processes[current_pid_get()].rseq_area = 0;
    g_processes[current_pid_get()].rseq_sig = 0;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

uint64_t process_get_current_rseq_area(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    uint64_t area = 0;
    if (is_valid_pid(current_pid_get())) {
        area = g_processes[current_pid_get()].rseq_area;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return area;
}

void process_rseq_update_on_preempt_locked(int32_t pid, uint32_t cpu_id)
{
    if (!is_valid_pid(pid)) {
        return;
    }
    process_t *proc = &g_processes[pid];
    if (proc->rseq_area == 0u) {
        return;
    }
    uint64_t area = proc->rseq_area;
    uint64_t cr3 = proc->cr3;
    if (cr3 == 0u ||
        !paging_is_user_range_mapped(cr3, area, PROCESS_RSEQ_AREA_SIZE)) {
        return;
    }

    uint32_t cpu = cpu_id;
    memcpy((void *)(uintptr_t)(area + PROCESS_RSEQ_CPU_ID_START),
           &cpu, sizeof(cpu));
    memcpy((void *)(uintptr_t)(area + PROCESS_RSEQ_CPU_ID),
           &cpu, sizeof(cpu));

    uint64_t rseq_cs = 0;
    memcpy(&rseq_cs, (const void *)(uintptr_t)(area + PROCESS_RSEQ_CS),
           sizeof(rseq_cs));
    if (rseq_cs == 0u) {
        return;
    }
    if (!paging_is_user_range_mapped(cr3, rseq_cs, 8u)) {
        return;
    }

    uint32_t cs_sig = 0;
    uint32_t cs_flags = 0;
    memcpy(&cs_sig, (const void *)(uintptr_t)(rseq_cs + PROCESS_RSEQ_CS_SIG),
           sizeof(cs_sig));
    memcpy(&cs_flags,
           (const void *)(uintptr_t)(rseq_cs + PROCESS_RSEQ_CS_FLAGS),
           sizeof(cs_flags));
    if (cs_sig != proc->rseq_sig) {
        proc->pending_signals |= (1u << 11u);
        return;
    }
    if ((cs_flags & PROCESS_RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT) != 0u) {
        return;
    }

    uint64_t zero = 0;
    memcpy((void *)(uintptr_t)(area + PROCESS_RSEQ_CS),
           &zero, sizeof(zero));
}

static int32_t process_create_user_internal(uint64_t entry,
                                            uint64_t arg1,
                                            uint64_t arg2,
                                            uint64_t arg3,
                                            uint64_t arg4,
                                            int start_ready)
{
    if (!process_table_ready() || !is_valid_user_entry(entry)) {
        return -1;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int32_t pid = find_free_slot();
    if (pid < 0) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    process_t *proc = &g_processes[pid];
    reset_process_slot(proc);

    proc->state = PROCESS_STATE_INIT;
    proc->memory_owner_pid = pid;

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    if (initialize_process_memory(proc, entry, arg1, arg2, arg3, arg4) < 0) {
        irq_flags = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        release_process_resources(proc);
        reset_process_slot(proc);
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    
    int32_t parent_pid = process_get_current_pid();

    irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (proc->state != PROCESS_STATE_INIT || proc->memory_owner_pid != pid) {
        release_process_resources(proc);
        reset_process_slot(proc);
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    proc->entry = entry;
    proc->timeslice = g_timeslice_ticks;
    proc->parent_pid = parent_pid;
    if (start_ready) {
        proc->state = PROCESS_STATE_READY;
        process_perf_mark_ready_locked(proc, process_perf_now_ns());
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    ipc_init_process_queue(pid);

    return pid;
}

int32_t process_create_user(uint64_t entry)
{
    return process_create_user_internal(entry, 0, 0, 0, 0, 1);
}

int32_t process_create_user_ex(uint64_t entry,
                               uint64_t arg1,
                               uint64_t arg2,
                               uint64_t arg3,
                               uint64_t arg4)
{
    return process_create_user_internal(entry, arg1, arg2, arg3, arg4, 1);
}

int32_t process_create_thread(uint64_t entry,
                              uint64_t arg1,
                              uint64_t arg2,
                              uint64_t arg3,
                              uint64_t arg4)
{
    if (!process_table_ready() || !is_valid_user_entry(entry)) {
        return -1;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int32_t current_tid = current_pid_get();
    if (!is_valid_pid(current_tid)) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    process_t *current = &g_processes[current_tid];
    process_t *owner = process_memory_owner_locked(current);
    if (owner == NULL || owner->cr3 == 0 ||
        owner->state == PROCESS_STATE_UNUSED ||
        owner->state == PROCESS_STATE_DEAD ||
        owner->state == PROCESS_STATE_ZOMBIE) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    int32_t tid = find_free_slot();
    if (tid < 0) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    int32_t owner_pid = (int32_t)(owner - g_processes);
    uint64_t region_top =
        owner->user_heap_limit -
        ((uint64_t)tid * PROCESS_THREAD_STACK_REGION_SIZE);
    uint64_t region_base = region_top - PROCESS_THREAD_STACK_REGION_SIZE;
    if (region_top <= region_base ||
        region_base < owner->user_heap_alloc_limit ||
        region_base < owner->user_heap_cursor) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    process_t *thread = &g_processes[tid];
    reset_process_slot(thread);
    thread->state = PROCESS_STATE_INIT;
    thread->is_thread = 1;
    thread->memory_owner_pid = owner_pid;
    thread->parent_pid = owner_pid;
    thread->cr3 = owner->cr3;
    thread->capability_mask = owner->capability_mask;
    thread->user_code_base = owner->user_code_base;
    thread->user_code_limit = owner->user_code_limit;
    thread->user_heap_base = owner->user_heap_base;
    thread->user_heap_cursor = owner->user_heap_cursor;
    thread->user_heap_limit = owner->user_heap_limit;
    thread->user_heap_alloc_limit = owner->user_heap_alloc_limit;
    thread->user_heap_guard_page = owner->user_heap_guard_page;
    thread->user_stack_guard_page = region_base;
    thread->user_stack_base = region_base + PROCESS_GUARD_PAGE_SIZE;
    thread->user_stack_top = region_top;
    thread->thread_stack_region_base = region_base;
    thread->thread_stack_region_size = PROCESS_THREAD_STACK_REGION_SIZE;
    memcpy(thread->signal_handlers, owner->signal_handlers,
           sizeof(thread->signal_handlers));
    thread->signal_mask = current->signal_mask;
    thread->abi_mode = owner->abi_mode;
    strncpy(thread->name, owner->name, sizeof(thread->name) - 1);
    thread->name[sizeof(thread->name) - 1] = '\0';

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    thread->kernel_stack_base = malloc(PROCESS_KERNEL_STACK_SIZE);
    if (thread->kernel_stack_base == NULL) {
        goto fail;
    }
    thread->kernel_stack_top =
        ((uint64_t)(uintptr_t)(thread->kernel_stack_base +
                               PROCESS_KERNEL_STACK_SIZE)) & ~0xFULL;

    if (paging_map_user_range_alloc(thread->cr3,
                                    thread->user_stack_base,
                                    PROCESS_THREAD_STACK_SIZE,
                                    PAGE_RW | PAGE_USER) < 0) {
        goto fail;
    }

    initialize_fpu_state(thread->fpu_state);
    thread->fs_base = current->fs_base;
    thread->gs_base = current->gs_base;

    uint64_t *kstack = (uint64_t *)thread->kernel_stack_top;
    kstack -= PROCESS_CONTEXT_QWORDS;
    for (uint32_t i = 0; i < PROCESS_CONTEXT_QWORDS; ++i) {
        kstack[i] = 0;
    }

#if defined(__aarch64__)
    arm64_exception_frame_t *frame = (arm64_exception_frame_t *)kstack;
    frame->x[0] = arg1;
    frame->x[1] = arg2;
    frame->x[2] = arg3;
    frame->x[3] = arg4;
    frame->elr_el1 = entry;
    frame->spsr_el1 = 0;
    thread->saved_rsp = (uint64_t)(uintptr_t)frame;
#else
    kstack[SYSCALL_FRAME_RCX] = entry;
    kstack[SYSCALL_FRAME_RDI] = arg1;
    kstack[SYSCALL_FRAME_RSI] = arg2;
    kstack[SYSCALL_FRAME_RDX] = arg3;
    kstack[SYSCALL_FRAME_R8] = arg4;
    kstack[SYSCALL_FRAME_R11] = PROCESS_RFLAGS_DEFAULT;
    thread->saved_rsp = (uint64_t)(uintptr_t)kstack;
#endif

    if (initialize_raw_user_stack(thread) < 0) {
        goto fail;
    }
#if defined(__aarch64__)
    frame->sp_el0 = thread->saved_user_rsp;
#endif

    irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (thread->state != PROCESS_STATE_INIT ||
        thread->memory_owner_pid != owner_pid) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        goto fail;
    }
    thread->entry = entry;
    thread->timeslice = g_timeslice_ticks;
    thread->state = PROCESS_STATE_READY;
    process_perf_mark_ready_locked(thread, process_perf_now_ns());
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return tid;

fail:
    irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    release_thread_resources(thread, 1);
    reset_process_slot(thread);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return -1;
}

int32_t process_spawn_user_elf_with_arg(const char *path,
                                        const char *launch_argument)
{
    if (!path || path[0] == '\0') {
        return -1;
    }
    int32_t pid = process_create_user_internal(USER_CODE_BASE, 0, 0, 0, 0, 0);
    if (pid < 0) {
        return -1;
    }

    process_t *proc = &g_processes[pid];

    const char *name_ptr = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' && *(p+1) != '\0') name_ptr = p + 1;
    }
    strncpy(proc->name, name_ptr, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';
    if (launch_argument) {
        strncpy(proc->launch_argument, launch_argument,
                sizeof(proc->launch_argument) - 1u);
        proc->launch_argument[sizeof(proc->launch_argument) - 1u] = '\0';
    }

    elf_load_policy_t policy = {
        .max_file_size = PROCESS_ELF_MAX_SIZE,
        .min_vaddr = 0x1000,
        .max_vaddr = USER_CODE_LIMIT,
    };
    elf_loaded_image_info_t image_info = {0};

    if (!elf_loader_load_from_path(proc->cr3, path, &policy, &image_info)) {
        const char *elf_err = elf_loader_last_error();
        extern void serial_write_string(const char *str);
        extern void serial_write_uint64(uint64_t value);
        serial_write_string("[spawn-fail] ");
        serial_write_string(path);
        serial_write_string(" err=");
        serial_write_string(elf_err ? elf_err : "?");
        serial_write_string("\n");
        ipc_cleanup_process_queue(pid);
        uint64_t irq_flags = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        release_process_resources(proc);
        reset_process_slot(proc);
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    proc->abi_mode = image_info.linux_abi ? PROCESS_ABI_LINUX : PROCESS_ABI_IMPLUS;
    proc->main_phdr_vaddr = image_info.phdr_vaddr;
    proc->main_phent = image_info.phent;
    proc->main_phnum = image_info.phnum;
    if (proc->abi_mode == PROCESS_ABI_LINUX) {
        const char *linux_argv[2];
        linux_argv[0] = name_ptr;
        uint64_t linux_argc = 1;
        if (launch_argument) {
            linux_argv[1] = launch_argument;
            linux_argc = 2;
        }
        if (initialize_elf_user_stack_ex(proc, &image_info, path,
                                         linux_argc, linux_argv, 0, NULL) < 0) {
            ipc_cleanup_process_queue(pid);
            uint64_t irq_flags = irq_save_disable();
            spinlock_lock(&g_process_table_lock);
            release_process_resources(proc);
            reset_process_slot(proc);
            spinlock_unlock(&g_process_table_lock);
            irq_restore(irq_flags);
            return -1;
        }
    } else if (initialize_elf_user_stack(proc, &image_info, path) < 0) {
        ipc_cleanup_process_queue(pid);
        uint64_t irq_flags = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        release_process_resources(proc);
        reset_process_slot(proc);
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    
    uint64_t irq_flags = irq_save_disable();

#if defined(__aarch64__)
    arm64_exception_frame_t *frame =
        (arm64_exception_frame_t *)(uintptr_t)proc->saved_rsp;
    frame->elr_el1 = image_info.entry;
    frame->sp_el0 = proc->saved_user_rsp;
#else
    uint64_t *kstack = (uint64_t *)(uintptr_t)proc->saved_rsp;
    kstack[SYSCALL_FRAME_RCX] = image_info.entry;
#endif

    spinlock_lock(&g_process_table_lock);
    mark_process_runnable(proc, image_info.entry, proc->parent_pid);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    return pid;
}

int32_t process_spawn_user_elf(const char *path)
{
    return process_spawn_user_elf_with_arg(path, NULL);
}

static int process_copy_user_page(uint64_t child_cr3, uint64_t vaddr,
                                   uint64_t parent_phys)
{
    void *child_page = pmm_alloc_pages(1);
    if (!child_page) return -1;
    memcpy(child_page, (void *)(uintptr_t)parent_phys, PAGE_SIZE);
    int ret = paging_map_user_page(child_cr3, vaddr,
                                    (uint64_t)(uintptr_t)child_page,
                                    PAGE_PRESENT | PAGE_RW | PAGE_USER);
    if (ret < 0) {
        pmm_free_pages(child_page, 1);
    }
    return ret;
}

static int process_clone_address_space(process_t *child, process_t *parent)
{
    if (parent->abi_mode == PROCESS_ABI_LINUX) {
        return paging_copy_present_user_range(child->cr3, parent->cr3,
                                              0x1000, USER_STACK_BASE);
    }

    uint64_t parent_cr3 = parent->cr3;
    uint64_t child_cr3 = child->cr3;

    for (uint64_t vaddr = USER_CODE_BASE; vaddr < USER_CODE_LIMIT;
         vaddr += PAGE_SIZE) {
        uint64_t phys = paging_virt_to_phys(parent_cr3, vaddr);
        if (phys == 0) continue;
        if (process_copy_user_page(child_cr3, vaddr, phys) < 0) return -1;
    }

    for (uint64_t vaddr = USER_HEAP_BASE; vaddr < parent->user_heap_cursor;
         vaddr += PAGE_SIZE) {
        uint64_t phys = paging_virt_to_phys(parent_cr3, vaddr);
        if (phys == 0) continue;
        if (process_copy_user_page(child_cr3, vaddr, phys) < 0) return -1;
    }

    for (uint64_t vaddr = parent->user_stack_base;
         vaddr < parent->user_stack_top; vaddr += PAGE_SIZE) {
        uint64_t phys = paging_virt_to_phys(parent_cr3, vaddr);
        if (phys == 0) continue;
        if (process_copy_user_page(child_cr3, vaddr, phys) < 0) return -1;
    }

    return 0;
}

int32_t process_fork(void)
{
    if (!process_table_ready()) return -1;

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int32_t current_pid = current_pid_get();
    if (!is_valid_pid(current_pid)) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -3;
    }

    process_t *parent = process_memory_owner_locked(&g_processes[current_pid]);
    if (!parent || parent->state == PROCESS_STATE_UNUSED) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -3;
    }

    if (parent->is_thread) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -38;
    }

    int32_t child_pid = find_free_slot();
    if (child_pid < 0) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    process_t *child = &g_processes[child_pid];
    reset_process_slot(child);
    child->state = PROCESS_STATE_INIT;
    child->memory_owner_pid = child_pid;

    int32_t parent_pid_saved = (int32_t)(parent - g_processes);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    child->kernel_stack_base = malloc(PROCESS_KERNEL_STACK_SIZE);
    if (!child->kernel_stack_base) goto fail_free;
    child->kernel_stack_top =
        (((uint64_t)(uintptr_t)(child->kernel_stack_base +
                                 PROCESS_KERNEL_STACK_SIZE)) & ~0xFULL);

    child->cr3 = paging_create_process_space();
    if (!child->cr3) goto fail_free_kstack;

    child->user_code_base = parent->user_code_base;
    child->user_code_limit = parent->user_code_limit;
    child->user_heap_base = parent->user_heap_base;
    child->user_heap_cursor = parent->user_heap_cursor;
    child->user_heap_limit = parent->user_heap_limit;
    child->user_heap_alloc_limit = parent->user_heap_alloc_limit;
    child->user_heap_guard_page = parent->user_heap_guard_page;
    child->user_stack_base = parent->user_stack_base;
    child->user_stack_top = parent->user_stack_top;
    child->user_stack_guard_page = parent->user_stack_guard_page;

    if (process_clone_address_space(child, parent) < 0) goto fail_free_space;

    for (uint32_t i = 0; i < PROCESS_USER_ALLOC_MAX; ++i) {
        child->user_allocs[i] = parent->user_allocs[i];
    }

    child->capability_mask = parent->capability_mask;
    child->abi_mode = parent->abi_mode;
    child->rseq_area = parent->rseq_area;
    child->rseq_sig = parent->rseq_sig;
    child->fs_base = parent->fs_base;
    child->gs_base = parent->gs_base;
    memcpy(child->fpu_state, parent->fpu_state, PROCESS_FPU_STATE_SIZE);

    uint64_t *parent_kstack = (uint64_t *)(uintptr_t)parent->saved_rsp;
    uint64_t *child_kstack = (uint64_t *)(uintptr_t)child->kernel_stack_top;
    child_kstack -= PROCESS_CONTEXT_QWORDS;
    for (uint32_t i = 0; i < PROCESS_CONTEXT_QWORDS; ++i) {
        child_kstack[i] = parent_kstack ? parent_kstack[i] : 0;
    }
    child_kstack[SYSCALL_FRAME_RAX] = 0;

    child->saved_rsp = (uint64_t)(uintptr_t)child_kstack;
    child->saved_user_rsp = parent->saved_user_rsp;

    irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (child->state != PROCESS_STATE_INIT) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        goto fail_free_space;
    }

    child->entry = parent->entry;
    child->parent_pid = parent_pid_saved;
    child->timeslice = g_timeslice_ticks;
    child->state = PROCESS_STATE_READY;
    process_perf_mark_ready_locked(child, process_perf_now_ns());

    memcpy(child->name, parent->name, sizeof(child->name));
    memcpy(child->cwd, parent->cwd, sizeof(child->cwd));
    memcpy(child->launch_argument, parent->launch_argument,
           sizeof(child->launch_argument));

    for (uint32_t i = 0; i < PROCESS_SIGNAL_MAX; ++i) {
        child->signal_handlers[i] = parent->signal_handlers[i];
    }
    child->signal_mask = parent->signal_mask;
    child->pending_signals = 0;

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    ipc_init_process_queue(child_pid);

    return child_pid;

fail_free_space:
    paging_destroy_process_space(child->cr3);
    child->cr3 = 0;
fail_free_kstack:
    free(child->kernel_stack_base);
    child->kernel_stack_base = NULL;
fail_free:
    irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    reset_process_slot(child);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return -1;
}

int32_t process_execve(const char *path, const char *const *argv,
                       const char *const *envp)
{
    if (!path || path[0] == '\0') return -22;

    char path_buf[256];
    uint64_t path_len = 0;
    if (!process_user_cstring_length(path, sizeof(path_buf) - 1, &path_len))
        return -14;
    if (copy_from_user(path_buf, path, path_len + 1) < path_len + 1)
        return -14;

    if (path_buf[0] != '/') {
        char cwd_buf[256];
        if (process_get_current_cwd(cwd_buf, sizeof(cwd_buf)) != 0) {
            return -36;
        }
        uint32_t cwd_len = (uint32_t)strlen(cwd_buf);
        while (cwd_len > 1u && cwd_buf[cwd_len - 1u] == '/') {
            cwd_buf[cwd_len - 1u] = '\0';
            --cwd_len;
        }
        if ((uint64_t)cwd_len + path_len + 2u > sizeof(path_buf)) {
            return -36;
        }
        memmove(path_buf + cwd_len + 1u, path_buf, path_len + 1u);
        memcpy(path_buf, cwd_buf, cwd_len);
        path_buf[cwd_len] = '/';
        path_len += (uint64_t)cwd_len + 1u;
    }

    uint64_t argc = 0;
    uint64_t envc = 0;
    uint64_t argv_strtotal = 0;
    uint64_t envp_strtotal = 0;

    if (count_user_string_array(argv, &argc, &argv_strtotal) < 0)
        return -22;
    if (count_user_string_array(envp, &envc, &envp_strtotal) < 0)
        return -22;

    (void)argv_strtotal;

    char strings_buf[EXECVE_STRTOTAL_MAX];
    uint64_t offsets[EXECVE_ARG_MAX];
    char *argv_ptrs[EXECVE_ARG_MAX];
    char *envp_ptrs[EXECVE_ARG_MAX];

    memset(strings_buf, 0, sizeof(strings_buf));

    if (argc > 0) {
        if (copy_user_strings(argv, argc, strings_buf, sizeof(strings_buf), offsets) < 0)
            return -14;
        for (uint64_t i = 0; i < argc; ++i)
            argv_ptrs[i] = strings_buf + offsets[i];
    }

    if (envc > 0) {
        uint64_t argv_len = (argc > 0) ? (offsets[argc - 1] + strlen(argv_ptrs[argc - 1]) + 1) : 0;
        if (argv_len + envp_strtotal > sizeof(strings_buf))
            return -22;
        if (copy_user_strings(envp, envc, strings_buf + argv_len, sizeof(strings_buf) - argv_len, offsets) < 0)
            return -14;
        for (uint64_t i = 0; i < envc; ++i)
            envp_ptrs[i] = strings_buf + argv_len + offsets[i];
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int32_t pid = current_pid_get();
    if (!is_valid_pid(pid)) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -3;
    }
    process_t *proc = process_memory_owner_locked(&g_processes[pid]);
    if (!proc || proc->state == PROCESS_STATE_UNUSED) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -3;
    }

    uint32_t closed_fds = 0;
    uint32_t closed_dirs = 0;
    int32_t proc_pid = (int32_t)(proc - g_processes);
    syscall_file_close_all_for_pid(proc_pid, &closed_fds, &closed_dirs);
    syscall_socket_close_all_for_pid(proc_pid);
    shared_memory_cleanup_process(proc_pid);

    uint64_t old_cr3 = proc->cr3;
    proc->cr3 = 0;
    proc->rseq_area = 0;
    proc->rseq_sig = 0;

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    paging_destroy_process_space(old_cr3);

    uint64_t new_cr3 = paging_create_process_space();
    if (!new_cr3) {
        uint64_t irq2 = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        proc->state = PROCESS_STATE_ZOMBIE;
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq2);
        return -1;
    }

    proc->cr3 = new_cr3;
    proc->user_code_base = USER_CODE_BASE;
    proc->user_code_limit = USER_CODE_LIMIT;
    proc->user_heap_base = USER_HEAP_BASE;
    proc->user_heap_cursor = USER_HEAP_BASE;
    proc->user_heap_limit = USER_HEAP_LIMIT;
    proc->user_stack_base = USER_STACK_BASE;
    proc->user_stack_top = USER_STACK_TOP;

    proc->user_heap_guard_page = proc->user_heap_limit - PROCESS_GUARD_PAGE_SIZE;
    proc->user_heap_limit -= PROCESS_GUARD_PAGE_SIZE;
    uint64_t thread_reserve =
        (uint64_t)g_process_capacity * PROCESS_THREAD_STACK_REGION_SIZE;
    if (thread_reserve >= (proc->user_heap_limit - proc->user_heap_base)) {
        paging_destroy_process_space(new_cr3);
        proc->cr3 = 0;
        uint64_t irq2 = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        proc->state = PROCESS_STATE_ZOMBIE;
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq2);
        return -1;
    }
    proc->user_heap_alloc_limit = proc->user_heap_limit - thread_reserve;
    proc->user_stack_guard_page = proc->user_stack_base;
    proc->user_stack_base += PROCESS_GUARD_PAGE_SIZE;

    uint64_t initial_stack_base =
        proc->user_stack_top - PROCESS_INITIAL_USER_STACK_SIZE;
    if (initial_stack_base < proc->user_stack_base ||
        paging_map_user_range_alloc(proc->cr3, initial_stack_base,
                                     PROCESS_INITIAL_USER_STACK_SIZE,
                                     PAGE_RW | PAGE_USER) < 0) {
        paging_destroy_process_space(new_cr3);
        proc->cr3 = 0;
        uint64_t irq2 = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        proc->state = PROCESS_STATE_ZOMBIE;
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq2);
        return -1;
    }

    const char *name_ptr = path_buf;
    for (const char *p = path_buf; *p; ++p) {
        if (*p == '/' && *(p+1) != '\0') name_ptr = p + 1;
    }
    strncpy(proc->name, name_ptr, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';

    for (uint32_t i = 0; i < PROCESS_USER_ALLOC_MAX; ++i) {
        proc->user_allocs[i].used = 0;
        proc->user_allocs[i].addr = 0;
        proc->user_allocs[i].size = 0;
    }

    elf_load_policy_t elf_policy = {
        .max_file_size = PROCESS_ELF_MAX_SIZE,
        .min_vaddr = 0x1000,
        .max_vaddr = USER_CODE_LIMIT,
    };
    elf_loaded_image_info_t image_info = {0};
    if (!elf_loader_load_from_path(proc->cr3, path_buf,
                                    &elf_policy, &image_info)) {
        paging_destroy_process_space(new_cr3);
        proc->cr3 = 0;
        uint64_t irq2 = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        proc->state = PROCESS_STATE_ZOMBIE;
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq2);
        return -1;
    }

    proc->abi_mode = image_info.linux_abi ? PROCESS_ABI_LINUX : PROCESS_ABI_IMPLUS;
    proc->main_phdr_vaddr = image_info.phdr_vaddr;
    proc->main_phent = image_info.phent;
    proc->main_phnum = image_info.phnum;

    if (initialize_elf_user_stack_ex(proc, &image_info, path_buf,
                                      argc, (const char *const *)argv_ptrs,
                                      envc, (const char *const *)envp_ptrs) < 0) {
        paging_destroy_process_space(new_cr3);
        proc->cr3 = 0;
        uint64_t irq2 = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        proc->state = PROCESS_STATE_ZOMBIE;
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq2);
        return -1;
    }

    uint64_t kstack_saved = proc->saved_rsp;
    if (kstack_saved != 0) {
        uint64_t *kstack = (uint64_t *)(uintptr_t)kstack_saved;
        kstack[SYSCALL_FRAME_RCX] = image_info.entry;
    }

    irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (proc->state != PROCESS_STATE_INIT &&
        proc->state != PROCESS_STATE_READY) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    proc->entry = image_info.entry;
    proc->timeslice = g_timeslice_ticks;
    proc->state = PROCESS_STATE_READY;
    proc->ready_since_ns = process_perf_now_ns();
    for (uint32_t i = 0; i < PROCESS_SIGNAL_MAX; ++i) {
        proc->signal_handlers[i] = 0;
    }
    proc->signal_mask = 0;
    proc->pending_signals = 0;
    proc->fs_base = 0;
    proc->gs_base = 0;

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    return 0;
}

int32_t process_copy_launch_argument(char *out, uint32_t capacity)
{
    if (!out || capacity == 0u) return -1;
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int32_t pid = current_pid_get();
    process_t *proc = is_valid_pid(pid) ? &g_processes[pid] : NULL;
    proc = process_memory_owner_locked(proc);
    if (!proc) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    uint32_t length = 0u;
    while (length + 1u < capacity &&
           proc->launch_argument[length] != '\0') {
        out[length] = proc->launch_argument[length];
        ++length;
    }
    out[length] = '\0';
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return (int32_t)length;
}

int64_t process_get_main_image_info(uint64_t *phdr_vaddr,
                                    uint64_t *phent,
                                    uint64_t *phnum)
{
    if (phdr_vaddr == NULL || phent == NULL || phnum == NULL) {
        return (int64_t)OS_STATUS_INVALID_ARG;
    }
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    process_t *proc = is_valid_pid(current_pid_get()) ?
        &g_processes[current_pid_get()] : NULL;
    proc = process_memory_owner_locked(proc);
    if (proc == NULL) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return (int64_t)OS_STATUS_FAULT;
    }
    *phdr_vaddr = proc->main_phdr_vaddr;
    *phent = proc->main_phent;
    *phnum = proc->main_phnum;

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int32_t process_register_boot_process(const char *path, uint64_t *entry_out)
{    int32_t pid = process_spawn_user_elf(path);
    if (pid < 0) {
        return -1;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    process_t *proc = &g_processes[pid];
    proc->state = PROCESS_STATE_RUNNING;
    current_pid_set(pid);
    proc->timeslice = g_timeslice_ticks;
    proc->ready_since_ns = 0u;
    proc->last_scheduled_ns = process_perf_now_ns();
    process_scheduler_consume_reschedule();
    if (entry_out) {
        *entry_out = proc->entry;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    return pid;
}

int32_t process_register_boot_process_from_memory(const void *data, uint64_t size, uint64_t *entry_out)
{
    if (!data || size == 0) return -1;

    int32_t pid = process_create_user_internal(USER_CODE_BASE, 0, 0, 0, 0, 0);
    if (pid < 0) {
        return -1;
    }

    process_t *proc = &g_processes[pid];
    strncpy(proc->name, "Userland.ELF", sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';

    elf_load_policy_t policy = {
        .max_file_size = PROCESS_ELF_MAX_SIZE,
        .min_vaddr = USER_CODE_BASE,
        .max_vaddr = USER_CODE_LIMIT,
    };
    elf_loaded_image_info_t image_info = {0};

    if (!elf_loader_load_from_memory(proc->cr3, data, size, &policy, &image_info)) {
        ipc_cleanup_process_queue(pid);
        uint64_t irq_flags = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        release_process_resources(proc);
        reset_process_slot(proc);
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    proc->main_phdr_vaddr = image_info.phdr_vaddr;
    proc->main_phent = image_info.phent;
    proc->main_phnum = image_info.phnum;

    if (initialize_elf_user_stack(proc, &image_info, "/Userland/Userland.ELF") < 0) {
        ipc_cleanup_process_queue(pid);
        uint64_t irq_flags = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        release_process_resources(proc);
        reset_process_slot(proc);
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    uint64_t irq_flags = irq_save_disable();

#if defined(__aarch64__)
    arm64_exception_frame_t *frame =
        (arm64_exception_frame_t *)(uintptr_t)proc->saved_rsp;
    frame->elr_el1 = image_info.entry;
    frame->sp_el0 = proc->saved_user_rsp;
#else
    uint64_t *kstack = (uint64_t *)(uintptr_t)proc->saved_rsp;
    kstack[SYSCALL_FRAME_RCX] = image_info.entry;
#endif

    spinlock_lock(&g_process_table_lock);
    proc->state = PROCESS_STATE_RUNNING;
    current_pid_set(pid);
    proc->timeslice = g_timeslice_ticks;
    proc->ready_since_ns = 0u;
    proc->last_scheduled_ns = process_perf_now_ns();
    process_scheduler_consume_reschedule();
    if (entry_out) {
        *entry_out = image_info.entry;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    return pid;
}

static int process_group_has_living_member_locked(int32_t leader_pid);

void process_exit_current(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return;
    }

    process_t *current = &g_processes[current_pid_get()];
    process_t *owner = process_memory_owner_locked(current);
    if (owner == NULL) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return;
    }
    int32_t pid_to_exit = (int32_t)(owner - g_processes);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    uint32_t closed_fds = 0;
    uint32_t closed_dirs = 0;
    syscall_file_close_all_for_pid(pid_to_exit, &closed_fds, &closed_dirs);
    syscall_socket_close_all_for_pid(pid_to_exit);
    shared_memory_cleanup_process(pid_to_exit);

    irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    g_processes[pid_to_exit].state = PROCESS_STATE_ZOMBIE;
    for (int32_t i = 1; i < g_process_capacity; ++i) {
        if (g_processes[i].is_thread &&
            g_processes[i].memory_owner_pid == pid_to_exit &&
            g_processes[i].state != PROCESS_STATE_UNUSED) {
            g_processes[i].state = PROCESS_STATE_DEAD;
        }
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    pnp_notifications_cleanup_process(pid_to_exit);
    ipc_cleanup_process_queue(pid_to_exit);
}

void process_exit_current_with_status(int32_t exit_status)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (is_valid_pid(current_pid_get())) {
        process_t *owner =
            process_memory_owner_locked(&g_processes[current_pid_get()]);
        if (owner != NULL) {
            owner->exit_status = exit_status;
        }
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    process_exit_current();
}

void process_thread_exit_current(int32_t exit_status)
{
    uint64_t clear_child_tid = 0;
    int32_t group_leader = -1;
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int32_t tid = current_pid_get();
    if (is_valid_pid(tid) && g_processes[tid].is_thread) {
        process_t *thread = &g_processes[tid];
        process_t *owner = process_memory_owner_locked(thread);
        if (owner != NULL) {
            group_leader = (int32_t)(owner - g_processes);
        }
        clear_child_tid = thread->clear_child_tid;
        thread->clear_child_tid = 0;
        thread->exit_status = exit_status;
        thread->state = thread->thread_detached ?
                        PROCESS_STATE_DEAD : PROCESS_STATE_ZOMBIE;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    if (clear_child_tid != 0u) {
        uint32_t zero = 0;
        if (copy_to_user((void *)(uintptr_t)clear_child_tid,
                         &zero, sizeof(zero)) == 0u) {
            (void)syscall_futex(clear_child_tid, 1u, 1u, 0u, 0u, 0u);
        }
    }

    if (group_leader >= 0 &&
        !process_group_has_living_member_locked(group_leader)) {
        process_exit_current();
    }
}

static int process_group_has_living_member_locked(int32_t leader_pid)
{
    if (!is_valid_pid(leader_pid)) {
        return 0;
    }
    process_t *leader = &g_processes[leader_pid];
    if (leader->state != PROCESS_STATE_UNUSED &&
        leader->state != PROCESS_STATE_DEAD &&
        leader->state != PROCESS_STATE_ZOMBIE) {
        return 1;
    }
    for (int32_t i = 1; i < g_process_capacity; ++i) {
        process_t *thread = &g_processes[i];
        if (thread->is_thread && thread->memory_owner_pid == leader_pid &&
            thread->state != PROCESS_STATE_UNUSED &&
            thread->state != PROCESS_STATE_DEAD &&
            thread->state != PROCESS_STATE_ZOMBIE) {
            return 1;
        }
    }
    return 0;
}

int32_t process_get_current_pid(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int32_t pid = -1;
    if (is_valid_pid(current_pid_get())) {
        process_t *owner =
            process_memory_owner_locked(&g_processes[current_pid_get()]);
        if (owner != NULL) {
            pid = (int32_t)(owner - g_processes);
        }
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return pid;
}

int32_t process_get_current_tid(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int32_t tid = is_valid_pid(current_pid_get()) ? current_pid_get() : -1;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return tid;
}

int process_thread_join(int32_t tid)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int32_t current_tid = current_pid_get();
    if (!is_valid_pid(current_tid) || !is_valid_pid(tid) ||
        tid == current_tid || !g_processes[tid].is_thread) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -22;
    }

    process_t *current_owner =
        process_memory_owner_locked(&g_processes[current_tid]);
    process_t *thread = &g_processes[tid];
    if (current_owner == NULL ||
        thread->memory_owner_pid != (int32_t)(current_owner - g_processes) ||
        thread->thread_detached) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -3;
    }
    if (thread->state != PROCESS_STATE_ZOMBIE) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -11;
    }
    if (process_scheduler_pid_in_use_on_any_cpu(tid)) {
        thread->state = PROCESS_STATE_DEAD;
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return 0;
    }

    release_thread_resources(thread, 1);
    reset_process_slot(thread);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int process_thread_detach(int32_t tid)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int32_t current_tid = current_pid_get();
    if (!is_valid_pid(current_tid) || !is_valid_pid(tid) ||
        !g_processes[tid].is_thread) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -3;
    }

    process_t *current_owner =
        process_memory_owner_locked(&g_processes[current_tid]);
    process_t *thread = &g_processes[tid];
    if (current_owner == NULL ||
        thread->memory_owner_pid != (int32_t)(current_owner - g_processes) ||
        thread->thread_detached) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -22;
    }

    thread->thread_detached = 1;
    if (thread->state == PROCESS_STATE_ZOMBIE) {
        thread->state = PROCESS_STATE_DEAD;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

uint64_t process_get_current_saved_rsp(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return 0;
    }
    uint64_t saved_rsp = g_processes[current_pid_get()].saved_rsp;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return saved_rsp;
}

uint64_t process_get_current_user_rsp(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return 0;
    }
    uint64_t rsp = g_processes[current_pid_get()].saved_user_rsp;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return rsp;
}

uint64_t process_get_current_cr3(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return paging_get_kernel_cr3();
    }
    uint64_t cr3 = g_processes[current_pid_get()].cr3;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return cr3;
}

const char *process_get_current_name_str(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return "none";
    }
    const char *name = g_processes[current_pid_get()].name;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return name;
}

uint64_t process_get_current_kernel_stack_base(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return 0;
    }
    uint64_t base = (uint64_t)(uintptr_t)
        g_processes[current_pid_get()].kernel_stack_base;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return base;
}

void process_debug_dump_current(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int32_t pid = current_pid_get();
    if (!is_valid_pid(pid)) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return;
    }
    process_t *proc = &g_processes[pid];
    extern void serial_write_uint64(uint64_t value);
    extern void serial_write_string(const char *str);
    serial_write_string("[PFDBG] pid=");
    serial_write_uint64((uint64_t)(uint32_t)pid);
    serial_write_string(" name=");
    serial_write_string(proc->name);
    serial_write_string(" is_thread=");
    serial_write_uint64((uint64_t)proc->is_thread);
    serial_write_string(" owner=");
    serial_write_uint64((uint64_t)(uint32_t)proc->memory_owner_pid);
    serial_write_string(" saved_rsp=");
    serial_write_uint64(proc->saved_rsp);
    serial_write_string(" saved_user_rsp=");
    serial_write_uint64(proc->saved_user_rsp);
    serial_write_string(" stack_top=");
    serial_write_uint64(proc->user_stack_top);
    serial_write_string(" heap_limit=");
    serial_write_uint64(proc->user_heap_limit);
    if (proc->saved_rsp != 0u) {
        const uint64_t *kf = (const uint64_t *)(uintptr_t)proc->saved_rsp;
        serial_write_string(" kf_rcx=");
        serial_write_string(" ");
        serial_write_uint64(kf[SYSCALL_FRAME_RCX]);
        serial_write_string(" kf_r11=");
        serial_write_uint64(kf[SYSCALL_FRAME_R11]);
        serial_write_string(" kf_rax=");
        serial_write_uint64(kf[SYSCALL_FRAME_RAX]);
    }
    serial_write_string("\n");
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
}

void process_debug_dump_slot_no_lock(int32_t pid)
{
    extern void serial_write_string(const char *str);
    extern void serial_write_uint64(uint64_t value);
    if (!is_valid_pid(pid)) {
        return;
    }
    process_t *proc = &g_processes[pid];
    serial_write_string("[SLOT] pid=");
    serial_write_uint64((uint64_t)(uint32_t)pid);
    serial_write_string(" name=");
    serial_write_string(proc->name);
    serial_write_string(" is_thread=");
    serial_write_uint64((uint64_t)proc->is_thread);
    serial_write_string(" owner=");
    serial_write_uint64((uint64_t)(uint32_t)proc->memory_owner_pid);
    serial_write_string(" parent=");
    serial_write_uint64((uint64_t)(uint32_t)proc->parent_pid);
    serial_write_string(" state=");
    serial_write_uint64((uint64_t)(uint32_t)proc->state);
    serial_write_string(" saved_rsp=");
    serial_write_uint64(proc->saved_rsp);
    serial_write_string(" saved_user_rsp=");
    serial_write_uint64(proc->saved_user_rsp);
    serial_write_string(" kstack_base=");
    serial_write_uint64((uint64_t)(uintptr_t)proc->kernel_stack_base);
    serial_write_string(" kstack_top=");
    serial_write_uint64(proc->kernel_stack_top);
    serial_write_string(" cr3=");
    serial_write_uint64(proc->cr3);
    serial_write_string("\n");
}

void process_debug_dump_pid(int32_t pid)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(pid)) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return;
    }
    process_t *proc = &g_processes[pid];
    extern void serial_write_uint64(uint64_t value);
    extern void serial_write_string(const char *str);
    serial_write_string("[PFDBG] pid=");
    serial_write_uint64((uint64_t)(uint32_t)pid);
    serial_write_string(" name=");
    serial_write_string(proc->name);
    serial_write_string(" is_thread=");
    serial_write_uint64((uint64_t)proc->is_thread);
    serial_write_string(" owner=");
    serial_write_uint64((uint64_t)(uint32_t)proc->memory_owner_pid);
    serial_write_string(" state=");
    serial_write_uint64((uint64_t)(uint32_t)proc->state);
    serial_write_string(" saved_rsp=");
    serial_write_uint64(proc->saved_rsp);
    serial_write_string(" saved_user_rsp=");
    serial_write_uint64(proc->saved_user_rsp);
    serial_write_string(" stack_top=");
    serial_write_uint64(proc->user_stack_top);
    serial_write_string(" heap_limit=");
    serial_write_uint64(proc->user_heap_limit);
    if (proc->saved_rsp != 0u) {
        const uint64_t *kf = (const uint64_t *)(uintptr_t)proc->saved_rsp;
        serial_write_string(" kf_rcx=");
        serial_write_string(" ");
        serial_write_uint64(kf[SYSCALL_FRAME_RCX]);
        serial_write_string(" kf_r11=");
        serial_write_uint64(kf[SYSCALL_FRAME_R11]);
        serial_write_string(" kf_rax=");
        serial_write_uint64(kf[SYSCALL_FRAME_RAX]);
        serial_write_string(" kf_rdi=");
        serial_write_uint64(kf[SYSCALL_FRAME_RDI]);
        serial_write_string(" kf_rsi=");
        serial_write_uint64(kf[SYSCALL_FRAME_RSI]);
        serial_write_string(" kf_rdx=");
        serial_write_uint64(kf[SYSCALL_FRAME_RDX]);
        serial_write_string(" kf_r8=");
        serial_write_uint64(kf[SYSCALL_FRAME_R8]);
    }
    serial_write_string("\n");
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
}

uint64_t process_schedule_on_syscall(uint64_t current_saved_rsp,
                                     uint64_t current_user_rsp,
                                     int request_switch,
                                     uint64_t *next_user_rsp_out)
{
    if (next_user_rsp_out != NULL) {
        *next_user_rsp_out = current_user_rsp;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    process_scheduler_clear_leaving_pid();
    uint64_t now_ns = process_perf_now_ns();
    int do_switch = (request_switch & PROCESS_SCHEDULE_REQUEST_SWITCH) != 0;
    int involuntary = (request_switch & PROCESS_SCHEDULE_REQUEST_INVOLUNTARY) != 0;

    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return current_saved_rsp;
    }

    process_t *current = &g_processes[current_pid_get()];
    if (do_switch) {
        process_perf_account_runtime_locked(current, now_ns);
    }
    if (current->state == PROCESS_STATE_RUNNING ||
        current->state == PROCESS_STATE_READY ||
        current->state == PROCESS_STATE_BLOCKED) {
        uint8_t original_state = current->state;
        save_syscall_frame_to_process(current, current_saved_rsp);
        if (current_user_rsp != 0) {
            current->saved_user_rsp = current_user_rsp;
        }
        process_fpu_save(current->fpu_state);
        
        current->fs_base = rdmsr_fs_base();
        current->gs_base = rdmsr_kernel_gs_base();

        if (do_switch && original_state != PROCESS_STATE_BLOCKED) {
            current->state = PROCESS_STATE_READY;
            process_perf_mark_ready_locked(current, now_ns);
        }
    }

    if (!do_switch &&
        (current->state == PROCESS_STATE_RUNNING ||
         current->state == PROCESS_STATE_READY)) {
        process_deliver_pending_signals_locked(current);
        uint64_t return_saved_rsp = current->saved_rsp;
        uint64_t return_user_rsp = current->saved_user_rsp;
        current->state = PROCESS_STATE_RUNNING;
        if (0) {
            extern void serial_write_uint64(uint64_t value);
            extern void serial_write_string(const char *str);
            if (current->is_thread) {
                serial_write_string("[SR> pid=");
                serial_write_uint64((uint64_t)(uint32_t)current_pid_get());
                serial_write_string(" srs=");
                serial_write_uint64(return_saved_rsp);
                serial_write_string(" rcx=");
                if (return_saved_rsp != 0u) {
                    serial_write_uint64(((const uint64_t *)(uintptr_t)return_saved_rsp)[13]);
                } else {
                    serial_write_uint64(0);
                }
                serial_write_string(" ursp=");
                serial_write_uint64(return_user_rsp);
                serial_write_string("]\n");
            }
        }
        (void)return_saved_rsp;
        (void)return_user_rsp;
        
        process_fpu_restore(current->fpu_state);
        activate_process_context(current);
        
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        
        if (next_user_rsp_out != NULL) {
            *next_user_rsp_out = return_user_rsp;
        }
        return return_saved_rsp;
    }

    int32_t next_pid = process_scheduler_pick_next(g_processes,
                                                   g_process_capacity,
                                                   current_pid_get());
                                                   
    static int embed_count = 0;
    while (next_pid < 0) {
        uint32_t ecpu = smp_get_current_cpu_id();
        if (ecpu != 0u && embed_count < 5) {
            embed_count++;
            debug_printf("[AP%u] embed_idle\r\n", ecpu);
        }
        spinlock_unlock(&g_process_table_lock);
        hal_cpu_enable_interrupts();
        uint64_t idle_start_ns = timer_monotonic_ns();
        process_cpu_halt();
        process_scheduler_add_idle_ns(timer_monotonic_ns() - idle_start_ns);
        hal_cpu_disable_interrupts();
        spinlock_lock(&g_process_table_lock);

        now_ns = process_perf_now_ns();
        process_wake_sleepers_locked(now_ns);
        next_pid = process_scheduler_pick_next(g_processes,
                                               g_process_capacity,
                                               current_pid_get());
    }

    int32_t old_pid = current_pid_get();
    if (next_pid >= 0 && next_pid != old_pid && old_pid >= 0) {
        process_scheduler_set_leaving_pid(old_pid);
    }

    current_pid_set(next_pid);
    process_t *next = &g_processes[current_pid_get()];
    process_deliver_pending_signals_locked(next);
    process_perf_prepare_run_locked(next, now_ns, involuntary);
    process_scheduler_prepare_run(next);

    uint64_t next_saved_rsp = next->saved_rsp;
    uint64_t next_user_rsp = next->saved_user_rsp;
        if (0) {
            extern void serial_write_uint64(uint64_t value);
            extern void serial_write_string(const char *str);
            if (next->is_thread || g_processes[current_pid_get()].is_thread) {
            serial_write_string("[SW> pid=");
            serial_write_uint64((uint64_t)(uint32_t)current_pid_get());
            serial_write_string(" srs=");
            serial_write_uint64(next_saved_rsp);
            serial_write_string(" srs_rcx=");
            if (next_saved_rsp != 0u) {
                serial_write_uint64(((const uint64_t *)(uintptr_t)next_saved_rsp)[13]);
            } else {
                serial_write_uint64(0);
            }
            serial_write_string(" srs_r11=");
            if (next_saved_rsp != 0u) {
                serial_write_uint64(((const uint64_t *)(uintptr_t)next_saved_rsp)[14]);
            } else {
                serial_write_uint64(0);
            }
            serial_write_string(" ursp=");
            serial_write_uint64(next_user_rsp);
            serial_write_string("]\n");
        }
    }
    (void)next_saved_rsp;
    (void)next_user_rsp;
    
    process_fpu_restore(next->fpu_state);
    activate_process_context(next);
    
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    if (next_user_rsp_out != NULL) {
        *next_user_rsp_out = next_user_rsp;
    }
    return next_saved_rsp;
}

uint64_t process_schedule_after_exit(uint64_t *next_user_rsp_out)
{
    if (next_user_rsp_out != NULL) {
        *next_user_rsp_out = 0;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    process_scheduler_clear_leaving_pid();
    uint64_t now_ns = process_perf_now_ns();

    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        halt_forever();
    }

    process_perf_account_runtime_locked(&g_processes[current_pid_get()], now_ns);

    int32_t next_pid = process_scheduler_pick_next(g_processes,
                                                   g_process_capacity,
                                                   current_pid_get());
    if (next_pid < 0) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        halt_forever();
    }

    int32_t old_pid = current_pid_get();
    if (next_pid >= 0 && next_pid != old_pid && old_pid >= 0) {
        process_scheduler_set_leaving_pid(old_pid);
    }

    current_pid_set(next_pid);
    process_t *next = &g_processes[current_pid_get()];
    process_deliver_pending_signals_locked(next);
    process_perf_prepare_run_locked(next, now_ns, 0);
    process_scheduler_prepare_run(next);

    uint64_t next_saved_rsp = next->saved_rsp;
    uint64_t next_user_rsp = next->saved_user_rsp;
    process_fpu_restore(next->fpu_state);
    activate_process_context(next);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    if (next_user_rsp_out != NULL) {
        *next_user_rsp_out = next_user_rsp;
    }
    return next_saved_rsp;
}

void process_on_timer_tick(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    process_wake_sleepers_locked(process_perf_now_ns());
    process_scheduler_on_tick(g_processes, g_process_capacity);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
}

int process_timeslice_expired(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int pending = process_scheduler_consume_reschedule();
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return pending;
}

int process_run_next_on_current_cpu(void)
{
    if (!process_table_ready()) {
        return 0;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    process_scheduler_clear_leaving_pid();

    int32_t current_pid = current_pid_get();
    process_rseq_update_on_preempt_locked(current_pid,
                                          smp_get_current_cpu_id());
    int32_t next_pid = process_scheduler_pick_next(g_processes,
                                                   g_process_capacity,
                                                   current_pid);
    if (next_pid < 0) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return 0;
    }

    if (next_pid != current_pid && current_pid >= 0) {
        process_scheduler_set_leaving_pid(current_pid);
    }

    current_pid_set(next_pid);
    process_t *next = &g_processes[next_pid];
    process_deliver_pending_signals_locked(next);
    process_perf_prepare_run_locked(next, process_perf_now_ns(), 0);
    process_scheduler_prepare_run(next);

    uint64_t next_saved_rsp = next->saved_rsp;
    uint64_t next_user_rsp = next->saved_user_rsp;
    uint64_t next_cr3 = next->cr3;
    process_fpu_restore(next->fpu_state);

    activate_process_context(next);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    const arch_ops_t *ops = arch_ops_get();
    if (ops != NULL && ops->enter_user_mode != NULL) {
        ops->enter_user_mode(next_saved_rsp, next_user_rsp, next_cr3);
    }
    return 0;
}

int process_user_buffer_is_valid(const void *ptr, uint64_t len)
{
    if (len == 0) {
        return 1;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return 0;
    }

    const process_t *proc = &g_processes[current_pid_get()];
    uint64_t addr           = (uint64_t)(uintptr_t)ptr;
    uint64_t code_base      = proc->user_code_base;
    uint64_t code_limit     = proc->user_code_limit;
    uint64_t heap_base      = proc->user_heap_base;
    uint64_t heap_limit     = proc->user_heap_limit;
    uint64_t stack_base     = proc->user_stack_base;
    uint64_t stack_top      = proc->user_stack_top;
    uint64_t process_cr3    = proc->cr3;
    uint8_t  abi_mode       = proc->abi_mode;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    if (abi_mode == PROCESS_ABI_LINUX) {
        if (addr < 0x1000) {
            return 0;
        }
        if (range_within(addr, len, 0x1000, USER_STACK_BASE)) {
            return paging_is_user_range_mapped(process_cr3, addr, len);
        }
        if (range_within(addr, len, stack_base, stack_top)) {
            return paging_is_user_range_mapped(process_cr3, addr, len);
        }
        return 0;
    }

    if (range_within(addr, len, code_base, code_limit)) {
        return paging_is_user_range_mapped(process_cr3, addr, len);
    }
    if (range_within(addr, len, heap_base, heap_limit)) {
        return paging_is_user_range_mapped(process_cr3, addr, len);
    }
    if (range_within(addr, len, stack_base, stack_top)) {
        return paging_is_user_range_mapped(process_cr3, addr, len);
    }
    return 0;
}

int process_user_cstring_length(const char *str, uint64_t max_len, uint64_t *len_out)
{
    if (str == NULL || max_len == 0) {
        return -1;
    }

    uint64_t current_page = (uint64_t)(uintptr_t)str & ~0xFFFULL;
    if (!process_user_buffer_is_valid((const void *)(uintptr_t)current_page, 1)) {
        return -1;
    }

    for (uint64_t i = 0; i < max_len; ++i) {
        uint64_t addr = (uint64_t)(uintptr_t)&str[i];
        if ((addr & ~0xFFFULL) != current_page) {
            current_page = addr & ~0xFFFULL;
            if (!process_user_buffer_is_valid((const void *)(uintptr_t)current_page, 1)) {
                return -1;
            }
        }

        char ch = '\0';
        if (copy_from_user(&ch, &str[i], 1u) != 0u) {
            return -1;
        }

        if (ch == '\0') {
            if (len_out != NULL) {
                *len_out = i;
            }
            return 0;
        }
    }
    return -1;
}

void *process_user_alloc(uint32_t size)
{
    if (size == 0) {
        return NULL;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return NULL;
    }

    process_t *proc =
        process_memory_owner_locked(&g_processes[current_pid_get()]);
    if (proc == NULL) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return NULL;
    }
    if (proc->user_heap_base == 0 ||
        proc->user_heap_limit <= proc->user_heap_base ||
        proc->user_heap_alloc_limit <= proc->user_heap_base ||
        proc->user_heap_cursor < proc->user_heap_base ||
        proc->user_heap_cursor > proc->user_heap_alloc_limit) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return NULL;
    }
    uint64_t alloc_size = 0;
    if (align_up_u64_checked((uint64_t)size, 16ULL, &alloc_size) < 0 ||
        alloc_size == 0 || alloc_size > UINT32_MAX) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return NULL;
    }

    for (uint32_t i = 0; i < PROCESS_USER_ALLOC_MAX; ++i) {
        user_alloc_t *slot = &proc->user_allocs[i];
        if (!slot->used && slot->size != 0 && slot->size >= alloc_size) {
            slot->used = 1;
            uint64_t addr = slot->addr;
            uint32_t slot_size = slot->size;
            uint64_t cr3 = proc->cr3;
            spinlock_unlock(&g_process_table_lock);
            irq_restore(irq_flags);
            if (paging_map_user_range_alloc(cr3, addr, alloc_size, PAGE_RW | PAGE_USER) < 0) {
                irq_flags = irq_save_disable();
                spinlock_lock(&g_process_table_lock);
                if (is_valid_pid(current_pid_get())) {
                    process_t *rollback_proc =
                        process_memory_owner_locked(
                            &g_processes[current_pid_get()]);
                    if (rollback_proc == NULL) {
                        spinlock_unlock(&g_process_table_lock);
                        irq_restore(irq_flags);
                        return NULL;
                    }
                    for (uint32_t j = 0; j < PROCESS_USER_ALLOC_MAX; ++j) {
                        if (rollback_proc->user_allocs[j].addr == addr &&
                            rollback_proc->user_allocs[j].size == slot_size) {
                            rollback_proc->user_allocs[j].used = 0;
                            break;
                        }
                    }
                }
                spinlock_unlock(&g_process_table_lock);
                irq_restore(irq_flags);
                return NULL;
            }
            return (void *)(uintptr_t)addr;
        }
    }

    uint32_t new_slot = PROCESS_USER_ALLOC_MAX;
    for (uint32_t i = 0; i < PROCESS_USER_ALLOC_MAX; ++i) {
        if (proc->user_allocs[i].size == 0) {
            new_slot = i;
            break;
        }
    }
    if (new_slot == PROCESS_USER_ALLOC_MAX) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return NULL;
    }

    uint64_t addr = 0;
    if (align_up_u64_checked(proc->user_heap_cursor, 16ULL, &addr) < 0) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return NULL;
    }
    uint64_t next = addr + alloc_size;
    if (next <= addr || next > proc->user_heap_alloc_limit) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return NULL;
    }

    uint64_t cr3 = proc->cr3;
    proc->user_heap_cursor = next;
    proc->user_allocs[new_slot].used = 1;
    proc->user_allocs[new_slot].addr = addr;
    proc->user_allocs[new_slot].size = (uint32_t)alloc_size;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    if (paging_map_user_range_alloc(cr3, addr, alloc_size, PAGE_RW | PAGE_USER) < 0) {
        irq_flags = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        if (is_valid_pid(current_pid_get())) {
            process_t *rollback_proc =
                process_memory_owner_locked(&g_processes[current_pid_get()]);
            if (rollback_proc == NULL) {
                spinlock_unlock(&g_process_table_lock);
                irq_restore(irq_flags);
                return NULL;
            }
            user_alloc_t *rollback_slot = &rollback_proc->user_allocs[new_slot];
            if (rollback_proc->cr3 == cr3 &&
                rollback_slot->used &&
                rollback_slot->addr == addr &&
                rollback_slot->size == (uint32_t)alloc_size) {
                rollback_slot->used = 0;
                rollback_slot->addr = 0;
                rollback_slot->size = 0;
                if (rollback_proc->user_heap_cursor == next) {
                    rollback_proc->user_heap_cursor = addr;
                }
            }
        }
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return NULL;
    }

    return (void *)(uintptr_t)addr;
}

int process_user_free(void *ptr)
{
    if (ptr == NULL) {
        return 0;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    process_t *proc =
        process_memory_owner_locked(&g_processes[current_pid_get()]);
    if (proc == NULL) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    uint64_t addr = (uint64_t)(uintptr_t)ptr;

    for (uint32_t i = 0; i < PROCESS_USER_ALLOC_MAX; ++i) {
        user_alloc_t *slot = &proc->user_allocs[i];
        if (slot->used && slot->addr == addr) {
            uint64_t cr3 = proc->cr3;
            uint32_t size = slot->size;
            slot->used = 0;
            spinlock_unlock(&g_process_table_lock);
            irq_restore(irq_flags);
            (void)paging_unmap_range(cr3, addr, size);
            return 0;
        }
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return -1;
}

int process_user_munmap(void *ptr, uint64_t length)
{
    if (ptr == NULL || length == 0) {
        return 0;
    }

    uint64_t start = (uint64_t)(uintptr_t)ptr & PAGE_MASK;
    uint64_t end = ((uint64_t)(uintptr_t)ptr + length + PAGE_SIZE - 1ULL) & PAGE_MASK;
    if (end <= start) {
        return -1;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    process_t *proc =
        process_memory_owner_locked(&g_processes[current_pid_get()]);
    if (proc == NULL) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    uint64_t cr3 = proc->cr3;
    for (uint32_t i = 0; i < PROCESS_USER_ALLOC_MAX; ++i) {
        user_alloc_t *slot = &proc->user_allocs[i];
        if (slot->size == 0) {
            continue;
        }
        uint64_t slot_start = slot->addr;
        uint64_t slot_end = slot->addr + slot->size;
        if (slot_start >= start && slot_end <= end) {
            slot->used = 0;
        }
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    return paging_unmap_range(cr3, start, end - start);
}

uint64_t process_get_heap_cursor(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    uint64_t cursor = 0;
    if (is_valid_pid(current_pid_get())) {
        cursor = g_processes[current_pid_get()].user_heap_cursor;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return cursor;
}

int process_set_heap_cursor(uint64_t addr)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    process_t *proc =
        process_memory_owner_locked(&g_processes[current_pid_get()]);
    uint64_t grow = 0;
    if (proc != NULL &&
        addr >= proc->user_heap_base &&
        addr <= proc->user_heap_alloc_limit) {
        if (addr > proc->user_heap_cursor) {
            grow = addr - proc->user_heap_cursor;
        } else {
            proc->user_heap_cursor = addr;
        }
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    if (grow != 0u) {
        if (process_user_alloc((uint32_t)grow) == NULL) {
            return -1;
        }
    }
    return 0;
}

void process_set_thread_user_rsp(int32_t tid, uint64_t user_rsp)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (is_valid_pid(tid)) {
        g_processes[tid].saved_user_rsp = user_rsp;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
}

uint64_t process_signal_set_handler(int32_t signum, uint64_t handler)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return (uint64_t)-1;
    }
    if (signum <= 0 || signum >= PROCESS_SIGNAL_MAX) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return (uint64_t)-1;
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    if (handler > 1u &&
        !process_user_buffer_is_valid((const void *)(uintptr_t)handler, 1)) {
        return (uint64_t)-1;
    }

    irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return (uint64_t)-1;
    }
    process_t *proc =
        process_memory_owner_locked(&g_processes[current_pid_get()]);
    if (proc == NULL) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return (uint64_t)-1;
    }
    uint64_t previous = proc->signal_handlers[(uint32_t)signum];
    proc->signal_handlers[(uint32_t)signum] = handler;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return previous;
}

uint64_t process_signal_get_handler(int32_t signum)
{
    if (signum <= 0 || signum >= PROCESS_SIGNAL_MAX) return (uint64_t)-1;
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    process_t *proc = NULL;
    if (is_valid_pid(current_pid_get())) {
        proc = process_memory_owner_locked(&g_processes[current_pid_get()]);
    }
    uint64_t handler = proc != NULL ?
        proc->signal_handlers[(uint32_t)signum] : (uint64_t)-1;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return handler;
}

uint64_t process_signal_get_mask(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    uint64_t mask = 0;
    if (is_valid_pid(current_pid_get())) {
        mask = g_processes[current_pid_get()].signal_mask;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return mask;
}

int process_signal_set_mask(uint64_t mask)
{
    const uint64_t unmaskable =
        (1ULL << (9u - 1u)) | (1ULL << (19u - 1u));
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -3;
    }
    g_processes[current_pid_get()].signal_mask = mask & ~unmaskable;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int process_signal_deliver(int32_t pid, int32_t signum)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (!is_valid_pid(pid)) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    if (signum <= 0 || signum >= PROCESS_SIGNAL_MAX) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    process_t *proc = &g_processes[pid];
    if (proc->state == PROCESS_STATE_UNUSED ||
        proc->state == PROCESS_STATE_DEAD ||
        proc->state == PROCESS_STATE_ZOMBIE) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    proc->pending_signals |= (1u << (uint32_t)signum);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

static int process_signal_blocked_locked(const process_t *proc, int32_t signum)
{
    if (signum == 9 || signum == 19) {
        return 0;
    }
    return (proc->signal_mask & (1ULL << ((uint32_t)signum - 1u))) != 0u;
}

int process_signal_validate_group(int32_t tgid, int32_t tid)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int valid = 0;
    if (is_valid_pid(tgid) && is_valid_pid(tid)) {
        process_t *thread = &g_processes[tid];
        if (thread->state != PROCESS_STATE_UNUSED &&
            thread->state != PROCESS_STATE_DEAD &&
            thread->state != PROCESS_STATE_ZOMBIE) {
            process_t *owner = process_memory_owner_locked(thread);
            if (owner != NULL &&
                (int32_t)(owner - g_processes) == tgid) {
                valid = 1;
            }
        }
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return valid;
}

int process_signal_deliver_group(int32_t pid, int32_t signum)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (pid == 0) {
        pid = current_pid_get();
        if (is_valid_pid(pid)) {
            process_t *owner =
                process_memory_owner_locked(&g_processes[pid]);
            if (owner != NULL) {
                pid = (int32_t)(owner - g_processes);
            }
        }
    }
    if (!is_valid_pid(pid)) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    process_t *leader = &g_processes[pid];
    if (leader->state == PROCESS_STATE_UNUSED ||
        leader->state == PROCESS_STATE_DEAD ||
        leader->state == PROCESS_STATE_ZOMBIE) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    leader = process_memory_owner_locked(leader);
    if (leader == NULL) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }
    int32_t leader_pid = (int32_t)(leader - g_processes);

    if (signum == 0) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return 0;
    }
    if (signum < 0 || signum >= PROCESS_SIGNAL_MAX) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    if (!process_signal_blocked_locked(leader, signum)) {
        leader->pending_signals |= (1u << (uint32_t)signum);
    } else {
        int32_t target = leader_pid;
        for (int32_t i = 1; i < g_process_capacity; ++i) {
            process_t *thread = &g_processes[i];
            if (thread->state == PROCESS_STATE_UNUSED ||
                thread->state == PROCESS_STATE_DEAD ||
                thread->state == PROCESS_STATE_ZOMBIE) {
                continue;
            }
            if (!thread->is_thread || thread->memory_owner_pid != leader_pid) {
                continue;
            }
            if (!process_signal_blocked_locked(thread, signum)) {
                target = i;
                break;
            }
        }
        g_processes[target].pending_signals |= (1u << (uint32_t)signum);
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int process_is_current_thread(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int is_thread = is_valid_pid(current_pid_get()) &&
                    g_processes[current_pid_get()].is_thread;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return is_thread;
}

int process_is_guard_page_fault(uint64_t fault_addr)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return 0;
    }

    const process_t *proc = &g_processes[current_pid_get()];
    uint64_t fault_page = fault_addr & PAGE_MASK;
    int is_guard = (fault_page == proc->user_heap_guard_page ||
                    fault_page == proc->user_stack_guard_page);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return is_guard;
}

process_capability_mask_t process_default_capabilities(void)
{
    return PROCESS_CAP_DEFAULT_MASK;
}

process_capability_mask_t process_get_current_capabilities(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return 0;
    }
    process_capability_mask_t mask = g_processes[current_pid_get()].capability_mask;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return mask;
}

int process_current_has_capability(process_capability_mask_t capability)
{
    if (capability == 0) {
        return 1;
    }

    process_capability_mask_t current = process_get_current_capabilities();
    return ((current & capability) == capability);
}

int process_set_capabilities(int32_t pid, process_capability_mask_t capabilities)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(pid)) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    g_processes[pid].capability_mask = capabilities & PROCESS_CAP_DEFAULT_MASK;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int process_get_capabilities(int32_t pid, process_capability_mask_t *capabilities_out)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(pid) || capabilities_out == NULL) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    *capabilities_out = g_processes[pid].capability_mask;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int process_is_alive(int32_t pid)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int alive = 0;
    if (is_valid_pid(pid)) {
        uint8_t state = g_processes[pid].state;
        alive = (state != PROCESS_STATE_UNUSED && state != PROCESS_STATE_DEAD && state != PROCESS_STATE_ZOMBIE);
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return alive;
}

int process_sleep_current_ms(uint64_t ms)
{
    if (ms == 0u) {
        return 0;
    }

    uint64_t delay_ns;
    if (ms > (UINT64_MAX / 1000000ULL)) {
        delay_ns = UINT64_MAX;
    } else {
        delay_ns = ms * 1000000ULL;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int32_t pid = current_pid_get();
    if (!is_valid_pid(pid) || g_sleep_deadline_ns == NULL ||
        g_processes[pid].state == PROCESS_STATE_UNUSED ||
        g_processes[pid].state == PROCESS_STATE_DEAD ||
        g_processes[pid].state == PROCESS_STATE_ZOMBIE) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    uint64_t now_ns = process_perf_now_ns();
    g_sleep_deadline_ns[pid] = process_deadline_after_ns(now_ns, delay_ns);
    g_processes[pid].state = PROCESS_STATE_BLOCKED;
    g_processes[pid].block_count++;
    g_processes[pid].blocked_since_ns = now_ns;
    g_processes[pid].ready_since_ns = 0u;
    process_scheduler_request_reschedule();

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int process_block_current(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    int32_t pid = current_pid_get();
    if (!is_valid_pid(pid) ||
        g_processes[pid].state == PROCESS_STATE_UNUSED ||
        g_processes[pid].state == PROCESS_STATE_DEAD ||
        g_processes[pid].state == PROCESS_STATE_ZOMBIE) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    uint64_t now_ns = process_perf_now_ns();
    if (g_sleep_deadline_ns != NULL) {
        g_sleep_deadline_ns[pid] = 0u;
    }
    g_processes[pid].state = PROCESS_STATE_BLOCKED;
    g_processes[pid].block_count++;
    g_processes[pid].blocked_since_ns = now_ns;
    g_processes[pid].ready_since_ns = 0u;
    process_scheduler_request_reschedule();
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int process_wake_pid(int32_t pid)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (!is_valid_pid(pid)) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    process_t *proc = &g_processes[pid];
    if (g_sleep_deadline_ns != NULL) {
        g_sleep_deadline_ns[pid] = 0u;
    }
    if (proc->state == PROCESS_STATE_BLOCKED) {
        uint64_t now_ns = process_perf_now_ns();
        if (proc->blocked_since_ns != 0u && now_ns >= proc->blocked_since_ns) {
            proc->blocked_ns += now_ns - proc->blocked_since_ns;
        }
        proc->blocked_since_ns = 0u;
        proc->wake_count++;
        proc->state = PROCESS_STATE_READY;
        process_perf_mark_ready_locked(proc, now_ns);
        process_scheduler_request_reschedule();
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int32_t process_waitpid(int32_t pid, int32_t *status_out, int32_t options)
{
    (void)options;
    int32_t my_pid = process_get_current_pid();
    if (my_pid < 0) {
        return -1;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (pid > 0) {
        if (!is_valid_pid(pid) || g_processes[pid].is_thread ||
            g_processes[pid].parent_pid != my_pid) {
            spinlock_unlock(&g_process_table_lock);
            irq_restore(irq_flags);
            return -1;
        }
        if (g_processes[pid].state == PROCESS_STATE_ZOMBIE &&
            !process_scheduler_pid_in_use_on_any_cpu(pid)) {
            int32_t exit_code = g_processes[pid].exit_status;
            release_process_resources(&g_processes[pid]);
            reset_process_slot(&g_processes[pid]);
            spinlock_unlock(&g_process_table_lock);
            irq_restore(irq_flags);
            if (status_out != NULL) {
                *status_out = exit_code;
            }
            return pid;
        }
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return 0;
    }
    
    for (int32_t i = 0; i < g_process_capacity; ++i) {
        if (g_processes[i].state == PROCESS_STATE_ZOMBIE &&
            !g_processes[i].is_thread &&
            g_processes[i].parent_pid == my_pid &&
            !process_scheduler_pid_in_use_on_any_cpu(i)) {
            int32_t exit_code = g_processes[i].exit_status;
            int32_t child_pid = i;
            release_process_resources(&g_processes[i]);
            reset_process_slot(&g_processes[i]);
            spinlock_unlock(&g_process_table_lock);
            irq_restore(irq_flags);
            if (status_out != NULL) {
                *status_out = exit_code;
            }
            return child_pid;
        }
    }
    
    int has_children = 0;
    for (int32_t i = 0; i < g_process_capacity; ++i) {
        if (i != my_pid && g_processes[i].state != PROCESS_STATE_UNUSED &&
            !g_processes[i].is_thread &&
            g_processes[i].parent_pid == my_pid) {
            has_children = 1;
            break;
        }
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    return has_children ? 0 : -1;
}

int32_t process_getppid(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int32_t pid = current_pid_get();
    int32_t ppid = -1;
    if (is_valid_pid(pid)) {
        process_t *owner = process_memory_owner_locked(&g_processes[pid]);
        if (owner != NULL) {
            ppid = owner->parent_pid;
        }
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return ppid;
}

int32_t process_get_parent_pid(int32_t pid)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int32_t ppid = -1;
    if (is_valid_pid(pid)) {
        ppid = g_processes[pid].parent_pid;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return ppid;
}
int32_t process_terminate(int32_t pid)
{
    if (pid < 0 || pid >= g_process_capacity) return -1;
    if (pid == current_pid_get()) {
        process_exit_current();
        return 0;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    process_t *proc = &g_processes[pid];
    if (proc->state == PROCESS_STATE_UNUSED || proc->state == PROCESS_STATE_DEAD) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    proc->state = PROCESS_STATE_DEAD;
    if (!process_scheduler_pid_in_use_on_any_cpu(pid)) {
        release_process_resources(proc);
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int32_t process_get_full_info(int32_t pid, void *info_out)
{
    if (pid < 0 || pid >= g_process_capacity) return -1;
    
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    process_t *proc = &g_processes[pid];
    if (proc->state == PROCESS_STATE_UNUSED) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    struct full_info {
        int32_t pid;
        int32_t parent_pid;
        uint8_t state;
        uint8_t reserved[7];
        char    name[64];
        uint64_t total_ticks;
        uint64_t memory_usage;
    } *info = info_out;

    info->pid = pid;
    info->parent_pid = proc->parent_pid;
    info->state = proc->state;
    for (int i = 0; i < 64; i++) info->name[i] = proc->name[i];
    info->total_ticks = proc->total_ticks;
    
    uint64_t usage = 0;
    if (proc->user_heap_cursor > proc->user_heap_base) {
        usage += (proc->user_heap_cursor - proc->user_heap_base);
    }
    if (proc->user_stack_top > proc->user_stack_base) {
        usage += (proc->user_stack_top - proc->user_stack_base);
    }
    info->memory_usage = usage;

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

static uint64_t process_memory_usage_locked(const process_t *proc)
{
    if (proc == NULL) {
        return 0;
    }
    uint64_t usage = 0;
    if (proc->user_heap_cursor > proc->user_heap_base) {
        usage += (proc->user_heap_cursor - proc->user_heap_base);
    }
    if (proc->user_stack_top > proc->user_stack_base) {
        usage += (proc->user_stack_top - proc->user_stack_base);
    }
    return usage;
}

int32_t process_get_perf_info(int32_t pid, process_perf_info_t *info_out)
{
    if (info_out == NULL || pid < 0 || pid >= g_process_capacity) {
        return -1;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    process_t *proc = &g_processes[pid];
    if (proc->state == PROCESS_STATE_UNUSED) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    }

    uint64_t now_ns = process_perf_now_ns();
    uint64_t runtime_ns = proc->runtime_ns;
    if (proc->state == PROCESS_STATE_RUNNING &&
        proc->last_scheduled_ns != 0u &&
        now_ns >= proc->last_scheduled_ns) {
        runtime_ns += now_ns - proc->last_scheduled_ns;
    }
    uint64_t blocked_ns = proc->blocked_ns;
    if (proc->state == PROCESS_STATE_BLOCKED &&
        proc->blocked_since_ns != 0u &&
        now_ns >= proc->blocked_since_ns) {
        blocked_ns += now_ns - proc->blocked_since_ns;
    }

    memset(info_out, 0, sizeof(*info_out));
    info_out->pid = pid;
    info_out->parent_pid = proc->parent_pid;
    info_out->state = proc->state;
    memcpy(info_out->name, proc->name, sizeof(info_out->name));
    info_out->runtime_ns = runtime_ns;
    info_out->ready_wait_ns = proc->ready_wait_ns;
    info_out->max_ready_wait_ns = proc->max_ready_wait_ns;
    info_out->context_switches = proc->context_switches;
    info_out->voluntary_switches = proc->voluntary_switches;
    info_out->involuntary_switches = proc->involuntary_switches;
    info_out->syscalls = proc->syscalls;
    info_out->ipc_send = proc->ipc_send;
    info_out->ipc_recv = proc->ipc_recv;
    info_out->block_count = proc->block_count;
    info_out->wake_count = proc->wake_count;
    info_out->blocked_ns = blocked_ns;
    info_out->display_present_calls = proc->display_present_calls;
    info_out->display_rects = proc->display_rects;
    info_out->display_bytes = proc->display_bytes;
    info_out->memory_usage = process_memory_usage_locked(proc);

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return 0;
}

void process_perf_note_syscall(int32_t pid)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (is_valid_pid(pid)) {
        g_processes[pid].syscalls++;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
}

void process_perf_note_ipc_send(int32_t pid)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (is_valid_pid(pid)) {
        g_processes[pid].ipc_send++;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
}

void process_perf_note_ipc_recv(int32_t pid)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (is_valid_pid(pid)) {
        g_processes[pid].ipc_recv++;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
}

void process_perf_note_display(int32_t pid, uint32_t rect_count, uint64_t bytes)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (is_valid_pid(pid)) {
        g_processes[pid].display_present_calls++;
        g_processes[pid].display_rects += rect_count;
        g_processes[pid].display_bytes += bytes;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
}

void process_debug_dump_summary(const char *reason)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    uint64_t now_ns = process_perf_now_ns();
    uint64_t total_runtime_ns = 0u;
    uint64_t total_display_bytes = 0u;
    int32_t top_pid = -1;
    uint64_t top_runtime_ns = 0u;
    char top_name[64];
    top_name[0] = '\0';

    for (int32_t i = 0; i < g_process_capacity; ++i) {
        process_t *proc = &g_processes[i];
        if (proc->state == PROCESS_STATE_UNUSED ||
            proc->state == PROCESS_STATE_DEAD) {
            continue;
        }
        uint64_t runtime_ns = proc->runtime_ns;
        if (proc->state == PROCESS_STATE_RUNNING &&
            proc->last_scheduled_ns != 0u &&
            now_ns >= proc->last_scheduled_ns) {
            runtime_ns += now_ns - proc->last_scheduled_ns;
        }
        total_runtime_ns += runtime_ns;
        total_display_bytes += proc->display_bytes;
        if (runtime_ns > top_runtime_ns) {
            top_runtime_ns = runtime_ns;
            top_pid = i;
            memcpy(top_name, proc->name, sizeof(top_name));
            top_name[sizeof(top_name) - 1u] = '\0';
        }
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    char line[192];
    snprintf(line, sizeof(line),
             "[process:perf] reason=%s total_runtime_ms=%llu display_bytes=%llu top_pid=%d top_runtime_ms=%llu top=%s\n",
             reason ? reason : "manual",
             (unsigned long long)(total_runtime_ns / 1000000ULL),
             (unsigned long long)total_display_bytes,
             (int)top_pid,
             (unsigned long long)(top_runtime_ns / 1000000ULL),
             top_name);
    serial_write_string(line);
}

void process_debug_dump_cpu_usage(const char *reason, uint64_t interval_ns)
{
    if (interval_ns == 0u) {
        interval_ns = 1000000000ULL;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    uint64_t now_ns = process_perf_now_ns();
    uint64_t total_delta_ns = 0u;
    uint64_t total_syscall_delta = 0u;
    uint64_t total_display_delta = 0u;
    uint64_t top_delta_ns = 0u;
    uint64_t top_user_delta_ns = 0u;
    uint64_t top_user_syscall_delta = 0u;
    uint64_t top_user_display_delta = 0u;
    uint64_t top_syscall_count = 0u;
    uint64_t top_syscall_runtime_delta = 0u;
    uint64_t user_delta_ns = 0u;
    uint64_t user_syscall_delta = 0u;
    int32_t top_pid = -1;
    int32_t top_user_pid = -1;
    int32_t top_syscall_pid = -1;
    char top_name[64];
    char top_user_name[64];
    char top_syscall_name[64];
    top_name[0] = '\0';
    top_user_name[0] = '\0';
    top_syscall_name[0] = '\0';

    int32_t capacity = g_process_capacity;
    if (capacity > OS_CONFIG_PROCESS_MAX_COUNT_MAX) {
        capacity = OS_CONFIG_PROCESS_MAX_COUNT_MAX;
    }

    for (int32_t i = 0; i < capacity; ++i) {
        process_t *proc = &g_processes[i];
        if (proc->state == PROCESS_STATE_UNUSED ||
            proc->state == PROCESS_STATE_DEAD) {
            g_cpu_usage_prev_runtime_ns[i] = 0u;
            g_cpu_usage_prev_syscalls[i] = 0u;
            g_cpu_usage_prev_display_bytes[i] = 0u;
            continue;
        }

        uint64_t runtime_ns = proc->runtime_ns;
        if (proc->state == PROCESS_STATE_RUNNING &&
            proc->last_scheduled_ns != 0u &&
            now_ns >= proc->last_scheduled_ns) {
            runtime_ns += now_ns - proc->last_scheduled_ns;
        }

        uint64_t runtime_delta = 0u;
        if (runtime_ns >= g_cpu_usage_prev_runtime_ns[i]) {
            runtime_delta = runtime_ns - g_cpu_usage_prev_runtime_ns[i];
        }
        uint64_t syscall_delta = 0u;
        if (proc->syscalls >= g_cpu_usage_prev_syscalls[i]) {
            syscall_delta = proc->syscalls - g_cpu_usage_prev_syscalls[i];
        }
        uint64_t display_delta = 0u;
        if (proc->display_bytes >= g_cpu_usage_prev_display_bytes[i]) {
            display_delta = proc->display_bytes -
                            g_cpu_usage_prev_display_bytes[i];
        }

        g_cpu_usage_prev_runtime_ns[i] = runtime_ns;
        g_cpu_usage_prev_syscalls[i] = proc->syscalls;
        g_cpu_usage_prev_display_bytes[i] = proc->display_bytes;

        total_delta_ns += runtime_delta;
        total_syscall_delta += syscall_delta;
        total_display_delta += display_delta;
        if (i != 0) {
            user_delta_ns += runtime_delta;
            user_syscall_delta += syscall_delta;
            if (runtime_delta > top_user_delta_ns) {
                top_user_delta_ns = runtime_delta;
                top_user_syscall_delta = syscall_delta;
                top_user_display_delta = display_delta;
                top_user_pid = i;
                memcpy(top_user_name, proc->name, sizeof(top_user_name));
                top_user_name[sizeof(top_user_name) - 1u] = '\0';
            }
        }
        if (runtime_delta > top_delta_ns) {
            top_delta_ns = runtime_delta;
            top_pid = i;
            memcpy(top_name, proc->name, sizeof(top_name));
            top_name[sizeof(top_name) - 1u] = '\0';
        }
        if (syscall_delta > top_syscall_count) {
            top_syscall_count = syscall_delta;
            top_syscall_runtime_delta = runtime_delta;
            top_syscall_pid = i;
            memcpy(top_syscall_name, proc->name, sizeof(top_syscall_name));
            top_syscall_name[sizeof(top_syscall_name) - 1u] = '\0';
        }
    }

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    uint32_t cpu_count = smp_get_cpu_count();
    if (cpu_count == 0u) {
        cpu_count = 1u;
    }
    uint64_t denominator = interval_ns * (uint64_t)cpu_count;
    if (denominator == 0u) {
        denominator = interval_ns;
    }
    uint64_t total_cpu_x100 = (total_delta_ns * 10000ULL) / denominator;
    uint64_t top_cpu_x100 = (top_delta_ns * 10000ULL) / denominator;
    uint64_t user_cpu_x100 = (user_delta_ns * 10000ULL) / denominator;
    uint64_t top_user_cpu_x100 =
        (top_user_delta_ns * 10000ULL) / denominator;
    uint64_t top_syscall_cpu_x100 =
        (top_syscall_runtime_delta * 10000ULL) / denominator;

    char line[384];
    snprintf(line, sizeof(line),
             "[cpu] reason=%s interval_ms=%llu cpus=%u total=%llu.%02llu%% user=%llu.%02llu%% syscalls=%llu user_syscalls=%llu display_kib=%llu top_pid=%d top=%s top_cpu=%llu.%02llu%% top_user_pid=%d top_user=%s top_user_cpu=%llu.%02llu%% top_user_syscalls=%llu top_user_display_kib=%llu syscall_pid=%d syscall_top=%s syscall_count=%llu syscall_cpu=%llu.%02llu%%\n",
             reason ? reason : "periodic",
             (unsigned long long)(interval_ns / 1000000ULL),
             (unsigned int)cpu_count,
             (unsigned long long)(total_cpu_x100 / 100ULL),
             (unsigned long long)(total_cpu_x100 % 100ULL),
             (unsigned long long)(user_cpu_x100 / 100ULL),
             (unsigned long long)(user_cpu_x100 % 100ULL),
             (unsigned long long)total_syscall_delta,
             (unsigned long long)user_syscall_delta,
             (unsigned long long)(total_display_delta / 1024ULL),
             (int)top_pid,
             top_name,
             (unsigned long long)(top_cpu_x100 / 100ULL),
             (unsigned long long)(top_cpu_x100 % 100ULL),
             (int)top_user_pid,
             top_user_name,
             (unsigned long long)(top_user_cpu_x100 / 100ULL),
             (unsigned long long)(top_user_cpu_x100 % 100ULL),
             (unsigned long long)top_user_syscall_delta,
             (unsigned long long)(top_user_display_delta / 1024ULL),
             (int)top_syscall_pid,
             top_syscall_name,
             (unsigned long long)top_syscall_count,
             (unsigned long long)(top_syscall_cpu_x100 / 100ULL),
             (unsigned long long)(top_syscall_cpu_x100 % 100ULL));
    serial_write_string(line);
}

int32_t process_get_capacity(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int32_t capacity = g_process_capacity;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return capacity;
}
