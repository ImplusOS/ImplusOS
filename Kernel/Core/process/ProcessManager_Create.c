#include "ProcessManager.h"

#include "IPC/IPC_Main.h"
#include "Core/elf/ELF_Loader.h"
#include "cpu/GDT_Main.h"
#include "MemoryManagement/Memory_Main.h"
#include "mmu/Paging_Main.h"
#include "Core/sync/Spinlock.h"
#include "Core/syscall/Syscall_File.h"
#include "Core/syscall/Syscall_Main.h"
#include "smp/SMP_Main.h"
#include <string.h>
#include "Debug/serial/Serial.h"

#include <stddef.h>
#include <stdint.h>

#define PROCESS_KERNEL_STACK_SIZE (256 * 1024)
#define PROCESS_USER_ALLOC_MAX 4096
#define PROCESS_SIGNAL_MAX 32
#define PROCESS_RFLAGS_DEFAULT 0x202ULL
#define PROCESS_GUARD_PAGE_SIZE PAGE_SIZE
#define PROCESS_INITIAL_USER_STACK_SIZE (16ULL * PAGE_SIZE)

#define PROCESS_STATE_UNUSED  0
#define PROCESS_STATE_READY   1
#define PROCESS_STATE_RUNNING 2
#define PROCESS_STATE_DEAD    3
#define PROCESS_STATE_INIT    4
#define PROCESS_STATE_ZOMBIE  5

#define PROCESS_CONTEXT_QWORDS SYSCALL_FRAME_QWORDS
#define PROCESS_ELF_MAX_SIZE (20ULL * 1024ULL * 1024ULL)

#define IA32_FS_BASE 0xC0000100U

static inline uint64_t rdmsr_fs_base(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(IA32_FS_BASE));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr_fs_base(uint64_t val)
{
    uint32_t lo = (uint32_t)(val & 0xFFFFFFFFU);
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" :: "c"(IA32_FS_BASE), "a"(lo), "d"(hi) : "memory");
}

typedef struct {
    uint8_t used;
    uint64_t addr;
    uint32_t size;
} user_alloc_t;

typedef struct __attribute__((aligned(16))) {
    uint8_t state;
    process_capability_mask_t capability_mask;
    uint64_t entry;
    uint64_t saved_rsp;
    uint64_t saved_user_rsp;
    uint8_t  fpu_state[512] __attribute__((aligned(16)));

    uint64_t fs_base;

    uint64_t cr3;
    uint8_t *kernel_stack_base;
    uint64_t kernel_stack_top;
    uint64_t user_code_base;
    uint64_t user_code_limit;
    uint64_t user_heap_base;
    uint64_t user_heap_cursor;
    uint64_t user_heap_limit;
    uint64_t user_heap_guard_page;
    uint64_t user_stack_base;
    uint64_t user_stack_top;
    uint64_t user_stack_guard_page;
    uint32_t timeslice;
    int32_t parent_pid;
    int32_t exit_status;
    user_alloc_t user_allocs[PROCESS_USER_ALLOC_MAX];
    uint64_t signal_handlers[PROCESS_SIGNAL_MAX];
} process_t;

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
static int32_t g_current_pid_per_cpu[OS_CONFIG_SMP_MAX_CPUS];
static spinlock_t g_process_table_lock;
static uint32_t g_timeslice_ticks = 4;
static int g_timeslice_resched = 0;

static void initialize_fpu_state(uint8_t fpu_state[512])
{
    if (fpu_state == NULL) {
        return;
    }

    memset(fpu_state, 0, 512);
    fpu_state[0] = 0x7F;
    fpu_state[1] = 0x03;
    fpu_state[24] = 0x80;
    fpu_state[25] = 0x1F;
}

int32_t current_pid_get(void)
{
    uint32_t cpu = smp_get_current_cpu_id();
    if (cpu >= OS_CONFIG_SMP_MAX_CPUS) cpu = 0;
    return g_current_pid_per_cpu[cpu];
}

static inline void current_pid_set(int32_t pid)
{
    uint32_t cpu = smp_get_current_cpu_id();
    if (cpu >= OS_CONFIG_SMP_MAX_CPUS) cpu = 0;
    g_current_pid_per_cpu[cpu] = pid;
}

static void halt_forever(void)
{
    while (1) {
        __asm__ volatile ("hlt");
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
    return (entry >= USER_CODE_BASE) &&
           (entry < USER_CODE_LIMIT);
}

static int process_table_ready(void)
{
    return g_processes != NULL && g_process_capacity > 0;
}

static int is_valid_pid(int32_t pid)
{
    return process_table_ready() && pid >= 0 && pid < g_process_capacity;
}

static void save_syscall_frame_to_process(process_t *proc, uint64_t current_saved_rsp)
{
    if (proc == NULL || current_saved_rsp == 0) return;
    proc->saved_rsp = current_saved_rsp;
}

static void reset_process_slot(process_t *proc)
{
    if (proc == NULL) {
        return;
    }

    proc->state = PROCESS_STATE_UNUSED;
    proc->capability_mask = 0;
    proc->entry = 0;
    proc->saved_rsp = 0;
    proc->saved_user_rsp = 0;

    initialize_fpu_state(proc->fpu_state);

    proc->fs_base = 0;

    proc->cr3 = 0;
    proc->kernel_stack_base = NULL;
    proc->kernel_stack_top = 0;
    proc->user_code_base = 0;
    proc->user_code_limit = 0;
    proc->user_heap_base = 0;
    proc->user_heap_cursor = 0;
    proc->user_heap_limit = 0;
    proc->user_heap_guard_page = 0;
    proc->user_stack_base = 0;
    proc->user_stack_top = 0;
    proc->user_stack_guard_page = 0;
    proc->timeslice = 0;
    proc->parent_pid = -1;
    proc->exit_status = 0;
    for (uint32_t i = 0; i < PROCESS_USER_ALLOC_MAX; ++i) {
        proc->user_allocs[i].used = 0;
        proc->user_allocs[i].addr = 0;
        proc->user_allocs[i].size = 0;
    }
    for (uint32_t i = 0; i < PROCESS_SIGNAL_MAX; ++i) {
        proc->signal_handlers[i] = 0;
    }
}

static void release_process_resources(process_t *proc)
{
    if (proc == NULL) {
        return;
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
}

static void release_process_table(void)
{
    if (!process_table_ready()) {
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
    g_process_capacity = 0;
    for (uint32_t i = 0; i < OS_CONFIG_SMP_MAX_CPUS; i++) {
        g_current_pid_per_cpu[i] = -1;
    }
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
        if (g_processes[i].state == PROCESS_STATE_DEAD && i != current_pid_get()) {
            release_process_resources(&g_processes[i]);
            reset_process_slot(&g_processes[i]);
            return i;
        }
    }

    for (int32_t i = 0; i < g_process_capacity; ++i) {
        if (g_processes[i].state == PROCESS_STATE_ZOMBIE && i != current_pid_get()) {
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

static int32_t pick_next_ready(int32_t current_pid)
{
    if (!process_table_ready()) {
        return -1;
    }

    if (current_pid < 0) {
        for (int32_t i = 0; i < g_process_capacity; ++i) {
            if (g_processes[i].state == PROCESS_STATE_READY ||
                g_processes[i].state == PROCESS_STATE_RUNNING) {
                return i;
            }
        }
        return -1;
    }

    for (int32_t step = 1; step <= g_process_capacity; ++step) {
        int32_t idx = (current_pid + step) % g_process_capacity;
        if (g_processes[idx].state == PROCESS_STATE_READY) {
            return idx;
        }
    }
    if (g_processes[current_pid].state == PROCESS_STATE_RUNNING ||
        g_processes[current_pid].state == PROCESS_STATE_READY) {
        return current_pid;
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
}

static void mark_process_runnable(process_t *proc, uint64_t entry, int32_t parent_pid)
{
    if (proc == NULL) {
        return;
    }

    proc->entry = entry;
    proc->parent_pid = parent_pid;
    proc->timeslice = g_timeslice_ticks;
    proc->state = PROCESS_STATE_READY;
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

static int initialize_elf_user_stack(process_t *proc,
                                     const elf_loaded_image_info_t *image_info,
                                     const char *exec_path)
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

    if (exec_path_len > PROCESS_INITIAL_USER_STACK_SIZE / 2ULL) {
        return -1;
    }

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

    sp &= ~0xFULL;
    
    sp -= (5ULL + ((uint64_t)(sizeof(auxv) / sizeof(auxv[0])) * 2ULL)) * sizeof(uint64_t);
    
    uint64_t *stack_words = (uint64_t *)(uintptr_t)sp;
    stack_words[0] = 1;
    stack_words[1] = execfn_addr;
    stack_words[2] = 0;
    stack_words[3] = 0;
    for (uint32_t i = 0; i < (uint32_t)(sizeof(auxv) / sizeof(auxv[0])); ++i) {
        stack_words[4 + (i * 2U)] = auxv[i].a_type;
        stack_words[5 + (i * 2U)] = auxv[i].a_val;
    }

    stack_words[4 + (sizeof(auxv) / sizeof(auxv[0])) * 2U] = 0;

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

    uint64_t *kstack = (uint64_t *)proc->kernel_stack_top;
    kstack -= PROCESS_CONTEXT_QWORDS;

    for (uint32_t i = 0; i < PROCESS_CONTEXT_QWORDS; ++i) {
        kstack[i] = 0;
    }

    kstack[SYSCALL_FRAME_RCX] = entry;
    kstack[SYSCALL_FRAME_RDI] = arg1;
    kstack[SYSCALL_FRAME_RSI] = arg2;
    kstack[SYSCALL_FRAME_RDX] = arg3;
    kstack[SYSCALL_FRAME_R8]  = arg4;
    kstack[SYSCALL_FRAME_R11] = PROCESS_RFLAGS_DEFAULT;

    proc->saved_rsp = (uint64_t)(uintptr_t)kstack;
    if (initialize_raw_user_stack(proc) < 0) {
        return -1;
    }
    proc->timeslice = g_timeslice_ticks;
    
    return 0;
}

void *process_user_mmap(uint64_t length, uint64_t flags)
{
    if (!is_valid_pid(current_pid_get()) || length == 0) {
        return NULL;
    }

    if ((flags & ~1ULL) != 0) {
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

    for (uint32_t i = 0; i < OS_CONFIG_SMP_MAX_CPUS; i++) {
        g_current_pid_per_cpu[i] = -1;
    }

    release_process_table();

    uint64_t table_size_u64 = (uint64_t)desired_capacity * (uint64_t)sizeof(process_t);
    if (table_size_u64 == 0 || table_size_u64 > 0xFFFFFFFFULL) {
        halt_forever();
    }

    g_processes = (process_t *)malloc((size_t)table_size_u64);
    if (g_processes == NULL) {
        halt_forever();
    }

    g_process_capacity = desired_capacity;
    for (int32_t i = 0; i < g_process_capacity; ++i) {
        reset_process_slot(&g_processes[i]);
    }
    
    g_processes[0].state = PROCESS_STATE_RUNNING;
    g_processes[0].parent_pid = -1;
    g_processes[0].capability_mask = PROCESS_CAP_DEFAULT_MASK;

    initialize_fpu_state(g_processes[0].fpu_state);
    g_processes[0].fs_base = 0;
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

    irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    proc->entry = entry;
    proc->timeslice = g_timeslice_ticks;
    proc->parent_pid = current_pid_get();
    if (start_ready) {
        proc->state = PROCESS_STATE_READY;
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

int32_t process_spawn_user_elf(const char *path)
{
    if (!path || path[0] == '\0') {
        return -1;
    }

    int32_t pid = process_create_user_internal(USER_CODE_BASE, 0, 0, 0, 0, 0);
    if (pid < 0) {
        return -1;
    }

    process_t *proc = &g_processes[pid];

    elf_load_policy_t policy = {
        .max_file_size = PROCESS_ELF_MAX_SIZE,
        .min_vaddr = USER_CODE_BASE,
        .max_vaddr = USER_CODE_LIMIT,
    };
    elf_loaded_image_info_t image_info = {0};

    serial_write_string("Loading ELF from path: ");
    serial_write_string(path);
    serial_write_string("\n");

    if (!elf_loader_load_from_path(proc->cr3, path, &policy, &image_info)) {
        serial_write_string("Failed to load ELF from path: ");
        serial_write_string(path);
        serial_write_string("\n");
        ipc_cleanup_process_queue(pid);
        uint64_t irq_flags = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        release_process_resources(proc);
        reset_process_slot(proc);
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    } else {
        serial_write_string("ELF loaded successfully\n");
    }

    if (initialize_elf_user_stack(proc, &image_info, path) < 0) {
        serial_write_string("Failed to initialize ELF user stack\n");
        ipc_cleanup_process_queue(pid);
        uint64_t irq_flags = irq_save_disable();
        spinlock_lock(&g_process_table_lock);
        release_process_resources(proc);
        reset_process_slot(proc);
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return -1;
    } else {
        serial_write_string("ELF user stack initialized successfully\n");
    }
    
    uint64_t irq_flags = irq_save_disable();

    uint64_t *kstack = (uint64_t *)(uintptr_t)proc->saved_rsp;
    kstack[SYSCALL_FRAME_RCX] = image_info.entry;

    spinlock_lock(&g_process_table_lock);
    mark_process_runnable(proc, image_info.entry, proc->parent_pid);
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    return pid;
}

int32_t process_register_boot_process(const char *path, uint64_t *entry_out)
{
    int32_t pid = process_spawn_user_elf(path);
    if (pid < 0) {
        return -1;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    process_t *proc = &g_processes[pid];
    proc->state = PROCESS_STATE_RUNNING;
    current_pid_set(pid);
    proc->timeslice = g_timeslice_ticks;
    g_timeslice_resched = 0;
    activate_process_context(proc);
    if (entry_out) {
        *entry_out = proc->entry;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    return pid;
}

void process_exit_current(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return;
    }

    int32_t pid_to_exit = current_pid_get();
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    uint32_t closed_fds = 0;
    uint32_t closed_dirs = 0;
    syscall_file_close_all_for_pid(pid_to_exit, &closed_fds, &closed_dirs);

    irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    g_processes[pid_to_exit].state = PROCESS_STATE_ZOMBIE;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    ipc_cleanup_process_queue(pid_to_exit);
}

void process_exit_current_with_status(int32_t exit_status)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (is_valid_pid(current_pid_get())) {
        g_processes[current_pid_get()].exit_status = exit_status;
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    process_exit_current();
}

int32_t process_get_current_pid(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    int32_t pid = current_pid_get();
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return pid;
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

    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        return current_saved_rsp;
    }

    process_t *current = &g_processes[current_pid_get()];
    if (current->state == PROCESS_STATE_RUNNING || current->state == PROCESS_STATE_READY) {
        save_syscall_frame_to_process(current, current_saved_rsp);
        if (current_user_rsp != 0) {
            current->saved_user_rsp = current_user_rsp;
        }
        __asm__ volatile("fxsave64 %0" : "=m"(current->fpu_state) :: "memory");
        
        current->fs_base = rdmsr_fs_base();

        current->state = PROCESS_STATE_READY;
    }

    if (!request_switch && current->state != PROCESS_STATE_DEAD) {
        uint64_t return_saved_rsp = current->saved_rsp;
        uint64_t return_user_rsp = current->saved_user_rsp;
        current->state = PROCESS_STATE_RUNNING;
        
        __asm__ volatile("fxrstor64 %0" :: "m"(current->fpu_state) : "memory");
        
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);

        activate_process_context(current);
        if (next_user_rsp_out != NULL) {
            *next_user_rsp_out = return_user_rsp;
        }
        return return_saved_rsp;
    }

    int32_t next_pid = pick_next_ready(current_pid_get());
    if (next_pid < 0) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        halt_forever();
    }

    current_pid_set(next_pid);
    process_t *next = &g_processes[current_pid_get()];
    next->state = PROCESS_STATE_RUNNING;
    next->timeslice = g_timeslice_ticks;
    g_timeslice_resched = 0;

    uint64_t next_saved_rsp = next->saved_rsp;
    uint64_t next_user_rsp = next->saved_user_rsp;
    
    __asm__ volatile("fxrstor64 %0" :: "m"(next->fpu_state) : "memory");
    
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    activate_process_context(next);

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

    if (!is_valid_pid(current_pid_get())) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        halt_forever();
    }

    int32_t next_pid = pick_next_ready(current_pid_get());
    if (next_pid < 0) {
        spinlock_unlock(&g_process_table_lock);
        irq_restore(irq_flags);
        halt_forever();
    }

    current_pid_set(next_pid);
    process_t *next = &g_processes[current_pid_get()];
    next->state = PROCESS_STATE_RUNNING;
    next->timeslice = g_timeslice_ticks;
    g_timeslice_resched = 0;

    uint64_t next_saved_rsp = next->saved_rsp;
    uint64_t next_user_rsp = next->saved_user_rsp;
    __asm__ volatile("fxrstor64 %0" :: "m"(next->fpu_state) : "memory");
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    activate_process_context(next);

    if (next_user_rsp_out != NULL) {
        *next_user_rsp_out = next_user_rsp;
    }
    return next_saved_rsp;
}

void process_on_timer_tick(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    if (is_valid_pid(current_pid_get())) {
        process_t *proc = &g_processes[current_pid_get()];
        if (proc->state == PROCESS_STATE_RUNNING) {
            if (proc->timeslice == 0) {
                proc->timeslice = g_timeslice_ticks;
            }
            if (proc->timeslice > 0) {
                proc->timeslice--;
                if (proc->timeslice == 0) {
                    g_timeslice_resched = 1;
                }
            }
        }
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
}

int process_timeslice_expired(void)
{
    int pending;
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);
    pending = g_timeslice_resched;
    g_timeslice_resched = 0;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return pending;
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
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

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

    for (uint64_t i = 0; i < max_len; ++i) {
        if (i == 0 || (((uintptr_t)&str[i] & 0xFFF) == 0)) {
            if (!process_user_buffer_is_valid(&str[i], 1)) {
                return -1;
            }
        }

        if (str[i] == '\0') {
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

    process_t *proc = &g_processes[current_pid_get()];
    if (proc->user_heap_base == 0 ||
        proc->user_heap_limit <= proc->user_heap_base ||
        proc->user_heap_cursor < proc->user_heap_base ||
        proc->user_heap_cursor > proc->user_heap_limit) {
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
            spinlock_unlock(&g_process_table_lock);
            irq_restore(irq_flags);
            return (void *)(uintptr_t)slot->addr;
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
    if (next <= addr || next > proc->user_heap_limit) {
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
            process_t *rollback_proc = &g_processes[current_pid_get()];
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

    process_t *proc = &g_processes[current_pid_get()];
    uint64_t addr = (uint64_t)(uintptr_t)ptr;

    for (uint32_t i = 0; i < PROCESS_USER_ALLOC_MAX; ++i) {
        user_alloc_t *slot = &proc->user_allocs[i];
        if (slot->used && slot->addr == addr) {
            slot->used = 0;
            spinlock_unlock(&g_process_table_lock);
            irq_restore(irq_flags);
            return 0;
        }
    }
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return -1;
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

    if (handler != 0 &&
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
    process_t *proc = &g_processes[current_pid_get()];
    uint64_t previous = proc->signal_handlers[(uint32_t)signum];
    proc->signal_handlers[(uint32_t)signum] = handler;
    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);
    return previous;
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
    uint64_t handler = proc->signal_handlers[(uint32_t)signum];

    spinlock_unlock(&g_process_table_lock);
    irq_restore(irq_flags);

    if (handler == 0) {
        return 0;
    }

    return 0;
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

int32_t process_waitpid(int32_t pid, int32_t *status_out, int32_t options)
{
    (void)options;
    int32_t my_pid = current_pid_get();
    if (my_pid < 0) {
        return -1;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_process_table_lock);

    if (pid > 0) {
        if (!is_valid_pid(pid) || g_processes[pid].parent_pid != my_pid) {
            spinlock_unlock(&g_process_table_lock);
            irq_restore(irq_flags);
            return -1;
        }
        if (g_processes[pid].state == PROCESS_STATE_ZOMBIE) {
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
            g_processes[i].parent_pid == my_pid) {
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
        ppid = g_processes[pid].parent_pid;
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