/*
 * ImplusOS user-space dynamic linker (ld-linux-x86-64.so.2 equivalent).
 *
 * Loads ELF shared objects (ET_DYN, x86-64, RELA), resolves DT_NEEDED
 * dependencies, binds symbols against the global scope, applies dynamic
 * relocations (including TLS), and provides dlopen/dlsym/dlclose/dlerror.
 *
 * TLS layout (verified against x86_64-elf-gcc 16 + binutils):
 *   - Code accesses TLS as:  mov %fs:0,reg ; var at disp(reg)
 *   - [TP] must equal TP (self pointer), where TP = FS base.
 *   - The main executable's static TLS block starts at
 *     TP - align_up(main_memsz, main_p_align)  (negative offsets).
 *   - Shared object blocks are placed above the TCB header at TP+0x10.
 *   - Module offsets relative to TP (rel_tp): main is negative,
 *     shared objects are positive.
 *
 * All relocations are bound eagerly (RTLD_LAZY is accepted but ignored);
 * GOT/PLT entries are therefore resolved before any library code runs.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

extern uint64_t syscall0(uint64_t num);
extern uint64_t syscall1(uint64_t num, uint64_t arg1);
extern uint64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3);

extern int32_t file_open(const char* path, uint64_t flags);
extern int64_t file_read(int32_t fd, void* buffer, uint64_t len);
extern int64_t file_seek(int32_t fd, int64_t offset, int32_t whence);
extern int32_t file_close(int32_t fd);

#define SYSCALL_PROCESS_YIELD         7ULL
#define SYSCALL_GETTID              158ULL
#define SYSCALL_ARCH_PRCTL          168ULL
#define SYSCALL_GET_MAIN_IMAGE_INFO 196ULL

#define LINUX_ARCH_SET_FS 0x1002u

#define PAGE_SIZE 4096ULL

#define DL_MAX_DSOS         64
#define DL_MAX_NEEDED       32
#define DL_MAX_TLS_MODULES  32
#define DL_MAX_THREAD_TLS  256

/* ------------------------------------------------------------------ */
/* ELF64 definitions (freestanding, no system elf.h available).       */
/* ------------------------------------------------------------------ */

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EM_X86_64 62
#define ET_DYN 3

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_TLS 7

#define PF_X 1
#define PF_W 2
#define PF_R 4

#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_PLTGOT 3
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_INIT 12
#define DT_FINI 13
#define DT_SONAME 14
#define DT_SYMBOLIC 16
#define DT_JMPREL 23
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_GNU_HASH 0x6ffffef5
#define DT_RELACOUNT 0x6ffffff9

#define STN_UNDEF 0
#define SHN_UNDEF 0
#define SHN_COMMON 0xfff2

#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_TLS 6

#define STB_GLOBAL 1
#define STV_HIDDEN 2
#define ELF64_ST_TYPE(i) ((i)&0xf)
#define ELF64_ST_VISIBILITY(o) ((o)&0x3)

#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_PLT32 4
#define R_X86_64_COPY 5
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8
#define R_X86_64_GOTPCREL 9
#define R_X86_64_32 10
#define R_X86_64_32S 11
#define R_X86_64_DTPMOD64 16
#define R_X86_64_DTPOFF64 17
#define R_X86_64_TPOFF64 18
#define R_X86_64_TLSGD 19
#define R_X86_64_TLSLD 20
#define R_X86_64_DTPOFF32 21
#define R_X86_64_GOTTPOFF 22
#define R_X86_64_TPOFF32 23
#define R_X86_64_IRELATIVE 37
#define R_X86_64_GOTPCRELX 41
#define R_X86_64_REX_GOTPCRELX 42

#define ELF64_R_SYM(info) ((info) >> 32)
#define ELF64_R_TYPE(info) ((info)&0xffffffffu)

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    uint64_t d_tag;
    union {
        uint64_t d_val;
        uint64_t d_ptr;
    } d_un;
} Elf64_Dyn;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Elf64_Rela;

/* ------------------------------------------------------------------ */
/* Core data structures.                                              */
/* ------------------------------------------------------------------ */

typedef struct dso dso_t;

struct dso {
    char name[256];
    char soname[64];
    uint8_t* map_addr;
    uint64_t map_size;
    uint64_t bias;
    const Elf64_Dyn* dyn;
    const char* strtab;
    const Elf64_Sym* symtab;
    const uint32_t* hash;
    const uint32_t* gnu_hash;
    uint64_t strsz;
    uint64_t syment;
    uint32_t sym_count;
    const Elf64_Rela* rela;
    uint64_t relasz;
    uint64_t rela_count;
    const Elf64_Rela* jmp_rela;
    uint64_t jmp_relasz;
    void (*init_fn)(void);
    void (*fini_fn)(void);
    void** init_array;
    uint64_t init_array_sz;
    void** fini_array;
    uint64_t fini_array_sz;
    uint64_t modid;
    int64_t tls_rel_tp;
    uint64_t tls_align;
    uint64_t tls_filesz;
    uint64_t tls_memsz;
    const void* tls_init;
    dso_t* needed[DL_MAX_NEEDED];
    uint32_t needed_count;
    uint32_t refcount;
    int is_main;
    int keep;
    int global;
    int visit_gen;
    void* tls_storage;
};

typedef struct {
    uint64_t phdr_vaddr;
    uint64_t phent;
    uint64_t phnum;
} os_main_image_info_t;

typedef struct {
    int64_t rel_tp;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
    const void* init;
} dl_tls_module_t;

typedef struct {
    int32_t tid;
    void* raw;
    uint64_t tp;
    uint64_t total;
} thread_tls_t;

static dso_t g_dsos[DL_MAX_DSOS];
static int g_dso_count;
static dso_t* g_scope[DL_MAX_DSOS];
static int g_scope_count;
static dso_t g_main_dso;
static int g_main_registered;
static int g_visit_gen;

static dl_tls_module_t g_tls_modules[DL_MAX_TLS_MODULES];
static int g_tls_module_count;
static int g_main_tls_registered;
static uint64_t g_tls_dso_cursor;
static uint64_t g_tls_max_align;
static int g_tls_layout_finalized;
static int32_t g_tls_pool_owner_tid;

static thread_tls_t g_thread_tls[DL_MAX_THREAD_TLS];

static volatile int g_lock;
static char g_error_buf[192];
static int g_have_error;
static int g_initialized;

typedef struct {
    uint64_t ti_module;
    uint64_t ti_offset;
} tls_index;

/* Symbols exported by the dynamic linker itself (it has no ELF symtab). */
extern void* __tls_get_addr(tls_index* ti);
static const char g_linker_strtab[] = "__tls_get_addr\0";
static const Elf64_Sym g_linker_syms[] = {
    {
        .st_name = 0,
        .st_info = (uint8_t)((STB_GLOBAL << 4) | STT_FUNC),
        .st_other = 0,
        .st_shndx = 0xFFF1u, /* SHN_ABS */
        .st_value = (uint64_t)(uintptr_t)&__tls_get_addr,
        .st_size = 0,
    },
};
static dso_t g_linker_dso;

static void dl_ensure_init(void);
static thread_tls_t* dl_tls_find_thread(int32_t tid);

/* ------------------------------------------------------------------ */
/* Small helpers.                                                     */
/* ------------------------------------------------------------------ */

static uint64_t dl_align_up(uint64_t value, uint64_t align)
{
    if (align == 0) align = 1;
    uint64_t mask = align - 1;
    return (value + mask) & ~mask;
}

static uint64_t dl_align_down(uint64_t value, uint64_t align)
{
    uint64_t mask = align - 1;
    return value & ~mask;
}

static void dl_lock(void)
{
    while (__sync_lock_test_and_set(&g_lock, 1) != 0) {
        (void)syscall0(SYSCALL_PROCESS_YIELD);
    }
}

static void dl_unlock(void)
{
    __sync_lock_release(&g_lock);
}

static void set_dlerror(const char* message)
{
    (void)snprintf(g_error_buf, sizeof(g_error_buf), "%s", message);
    g_have_error = 1;
}

static void dl_dbg(const char* msg)
{
    extern void serial_write_string(const char* str);
    serial_write_string("[dlfcn] ");
    serial_write_string(msg);
    serial_write_string("\n");
}

static void dl_tls_set_fs(uint64_t tp)
{
    (void)syscall2(SYSCALL_ARCH_PRCTL, (uint64_t)LINUX_ARCH_SET_FS, tp);
}

static uint64_t dl_get_tp(void)
{
    uint64_t tp = 0;
    __asm__ volatile("movq %%fs:0, %0" : "=r"(tp));
    return tp;
}

static const Elf64_Sym* dl_dynsym_at(dso_t* d, uint64_t index)
{
    if (d->symtab == NULL) return NULL;
    if (d == &g_linker_dso) {
        return (index < d->sym_count) ? &d->symtab[index] : NULL;
    }
    uint64_t start = (uint64_t)(uintptr_t)d->symtab;
    uint64_t end = (uint64_t)(uintptr_t)d->map_addr + d->map_size;
    if (start < (uint64_t)(uintptr_t)d->map_addr || start >= end) return NULL;
    uint64_t offset = index * sizeof(Elf64_Sym);
    if (offset > end - start) return NULL;
    if (offset + sizeof(Elf64_Sym) > end - start) return NULL;
    return &d->symtab[index];
}

/* ------------------------------------------------------------------ */
/* Hash table based symbol lookup.                                    */
/* ------------------------------------------------------------------ */

static uint32_t dl_elf_hash(const char* name)
{
    uint32_t hash = 0;
    while (*name != '\0') {
        hash = (hash << 4) + (uint8_t)*name++;
        uint32_t g = hash & 0xf0000000u;
        if (g != 0) hash ^= g >> 24;
        hash &= ~g;
    }
    return hash;
}

static uint32_t dl_gnu_hash(const char* name)
{
    uint32_t hash = 5381;
    while (*name != '\0') {
        hash = hash * 33u + (uint8_t)*name++;
    }
    return hash;
}

static const Elf64_Sym* dl_sym_by_name(dso_t* d, const char* name)
{
    if (d->symtab == NULL || d->strtab == NULL) return NULL;

    if (d->gnu_hash != NULL) {
        const uint32_t* ht = d->gnu_hash;
        uint32_t nbuckets = ht[0];
        uint32_t symoffset = ht[1];
        uint32_t bloom_size = ht[2];
        uint32_t hash = dl_gnu_hash(name);
        uint32_t bucket_index = hash % nbuckets;
        const uint32_t* buckets = ht + 4 + bloom_size;
        uint32_t sym_index = buckets[bucket_index];
        if (sym_index < symoffset) return NULL;
        const uint32_t* chain = buckets + nbuckets;
        uint64_t chain_end = (uint64_t)(uintptr_t)d->map_addr + d->map_size;
        uint64_t chain_start = (uint64_t)(uintptr_t)chain;
        uint32_t max_index = 0;
        if (chain_start < chain_end) {
            uint64_t entries = (chain_end - chain_start) / 4u;
            if (entries > 200000u) entries = 200000u;
            max_index = (uint32_t)entries;
        }
        uint32_t count = 0;
        while (count < max_index) {
            count++;
            uint32_t chain_value = chain[sym_index - symoffset];
            if (sym_index >= d->sym_count) return NULL;
            if ((chain_value & 0xfffffffeu) != (hash & 0xfffffffeu)) {
                if ((chain_value & 1u) != 0) return NULL;
                sym_index++;
                continue;
            }
            const Elf64_Sym* sym = dl_dynsym_at(d, sym_index);
            if (sym != NULL && sym->st_name < d->strsz) {
                const char* sym_name = d->strtab + sym->st_name;
                if (strcmp(sym_name, name) == 0) return sym;
            }
            if ((chain_value & 1u) != 0) return NULL;
            sym_index++;
        }
        return NULL;
    }

    if (d->hash != NULL) {
        uint32_t nbucket = d->hash[0];
        uint32_t nchain = d->hash[1];
        uint32_t hash = dl_elf_hash(name);
        const uint32_t* buckets = d->hash + 2;
        const uint32_t* chains = buckets + nbucket;
        uint32_t index = buckets[hash % nbucket];
        uint32_t max_index = d->sym_count;
        if (nchain < max_index) max_index = nchain;
        while (index != STN_UNDEF && index < max_index) {
            const Elf64_Sym* sym = dl_dynsym_at(d, index);
            if (sym != NULL && sym->st_name < d->strsz) {
                const char* sym_name = d->strtab + sym->st_name;
                if (strcmp(sym_name, name) == 0) return sym;
            }
            index = chains[index];
        }
        return NULL;
    }

    for (uint32_t i = 0; i < d->sym_count; i++) {
        const Elf64_Sym* sym = dl_dynsym_at(d, i);
        if (sym == NULL) continue;
        if (sym->st_name >= d->strsz) continue;
        const char* sym_name = d->strtab + sym->st_name;
        if (sym_name[0] == '\0') continue;
        if (strcmp(sym_name, name) == 0) return sym;
    }
    return NULL;
}

static const Elf64_Sym* dl_lookup_in_dso(dso_t* d, const char* name)
{
    if (d == NULL || d->is_main) return NULL;
    const Elf64_Sym* sym = dl_sym_by_name(d, name);
    if (sym == NULL) return NULL;
    if (sym->st_shndx == SHN_UNDEF) return NULL;
    if (sym->st_shndx == SHN_COMMON) return NULL;
    if (ELF64_ST_VISIBILITY(sym->st_other) == STV_HIDDEN) return NULL;
    return sym;
}

static const Elf64_Sym* dl_lookup_local_group(
    dso_t* root, const char* name, dso_t** def_out)
{
    g_visit_gen++;
    dso_t* stack[DL_MAX_DSOS];
    int depth = 0;
    if (root != NULL) {
        stack[depth++] = root;
    }
    while (depth > 0) {
        dso_t* d = stack[--depth];
        if (d->visit_gen == g_visit_gen) continue;
        d->visit_gen = g_visit_gen;
        const Elf64_Sym* sym = dl_lookup_in_dso(d, name);
        if (sym != NULL) {
            *def_out = d;
            return sym;
        }
        for (uint32_t i = 0; i < d->needed_count && depth < DL_MAX_DSOS; i++) {
            if (d->needed[i] != NULL &&
                d->needed[i]->visit_gen != g_visit_gen) {
                stack[depth++] = d->needed[i];
            }
        }
    }
    return NULL;
}

static const Elf64_Sym* dl_lookup(
    const char* name, dso_t* caller, dso_t** def_out)
{
    *def_out = NULL;

    for (int i = 0; i < g_scope_count; i++) {
        const Elf64_Sym* sym = dl_lookup_in_dso(g_scope[i], name);
        if (sym != NULL) {
            *def_out = g_scope[i];
            return sym;
        }
    }

    const Elf64_Sym* sym = dl_lookup_local_group(caller, name, def_out);
    if (sym != NULL) return sym;

    sym = dl_lookup_in_dso(&g_linker_dso, name);
    if (sym != NULL) {
        *def_out = &g_linker_dso;
        return sym;
    }
    return NULL;
}

static uint64_t dl_symbol_value(dso_t* def, const Elf64_Sym* sym)
{
    if (def == &g_linker_dso) return sym->st_value;
    return def->bias + sym->st_value;
}

/* ------------------------------------------------------------------ */
/* File reading.                                                      */
/* ------------------------------------------------------------------ */

static uint8_t* dl_read_whole_file(const char* path, uint64_t* size_out)
{
    int32_t fd = file_open(path, O_RDONLY);
    if (fd < 0) return NULL;
    int64_t end = file_seek(fd, 0, SEEK_END);
    if (end <= 0) {
        file_close(fd);
        return NULL;
    }
    if (file_seek(fd, 0, SEEK_SET) != 0) {
        file_close(fd);
        return NULL;
    }
    uint64_t size = (uint64_t)end;
    uint8_t* buffer = (uint8_t*)malloc(size);
    if (buffer == NULL) {
        file_close(fd);
        return NULL;
    }
    uint64_t total = 0;
    while (total < size) {
        int64_t count = file_read(fd, buffer + total, size - total);
        if (count <= 0) {
            free(buffer);
            file_close(fd);
            return NULL;
        }
        total += (uint64_t)count;
    }
    file_close(fd);
    *size_out = size;
    return buffer;
}

/* ------------------------------------------------------------------ */
/* TLS layout.                                                        */
/* ------------------------------------------------------------------ */

static int dl_tls_register_main(void)
{
    os_main_image_info_t info;
    memset(&info, 0, sizeof(info));
    int64_t status = (int64_t)syscall1(
        SYSCALL_GET_MAIN_IMAGE_INFO, (uint64_t)(uintptr_t)&info);
    if (status < 0) return -1;
    if (info.phdr_vaddr == 0 || info.phnum == 0) return 0;
    if (info.phent < sizeof(Elf64_Phdr)) return 0;

    const uint8_t* phdr_table = (const uint8_t*)(uintptr_t)info.phdr_vaddr;
    for (uint64_t i = 0; i < info.phnum; i++) {
        const Elf64_Phdr* phdr =
            (const Elf64_Phdr*)(const void*)(phdr_table + i * info.phent);
        if (phdr->p_type != PT_TLS) continue;
        uint64_t align = phdr->p_align;
        if (align < 16) align = 16;
        if (align > g_tls_max_align) g_tls_max_align = align;
        g_tls_modules[g_tls_module_count].rel_tp =
            -(int64_t)dl_align_up(phdr->p_memsz, align);
        g_tls_modules[g_tls_module_count].filesz = phdr->p_filesz;
        g_tls_modules[g_tls_module_count].memsz = phdr->p_memsz;
        g_tls_modules[g_tls_module_count].align = align;
        g_tls_modules[g_tls_module_count].init =
            (const void*)(uintptr_t)phdr->p_vaddr;
        g_tls_module_count++;
        g_main_tls_registered = 1;
        break;
    }
    return 0;
}

static int dl_tls_register_dso(dso_t* d)
{
    if (g_tls_layout_finalized) return -1;
    if (g_tls_module_count >= DL_MAX_TLS_MODULES) return -1;
    uint64_t align = d->tls_align;
    if (align < 16) align = 16;
    if (g_tls_dso_cursor == 0 && g_tls_module_count >= 1) {
        g_tls_dso_cursor =
            dl_align_up(g_tls_modules[0].memsz, g_tls_modules[0].align);
    }
    int64_t rel = -(int64_t)dl_align_up(0x10 + g_tls_dso_cursor, align);
    g_tls_dso_cursor = (uint64_t)(-rel) + d->tls_memsz;
    if (align > g_tls_max_align) g_tls_max_align = align;
    g_tls_modules[g_tls_module_count].rel_tp = rel;
    g_tls_modules[g_tls_module_count].filesz = d->tls_filesz;
    g_tls_modules[g_tls_module_count].memsz = d->tls_memsz;
    g_tls_modules[g_tls_module_count].align = align;
    g_tls_modules[g_tls_module_count].init = d->tls_init;
    g_tls_module_count++;
    d->tls_rel_tp = rel;
    d->modid = (uint64_t)g_tls_module_count;
    return 0;
}

static uint64_t dl_tls_pool_total(void)
{
    return 0x10u + 0x10u + g_tls_dso_cursor;
}

static void dl_tls_pool_fill(uint64_t pool, uint64_t tp)
{
    (void)pool;
    for (int i = 0; i < g_tls_module_count; i++) {
        const dl_tls_module_t* m = &g_tls_modules[i];
        uint8_t* block =
            (uint8_t*)(uintptr_t)(tp + (uint64_t)m->rel_tp);
        if (m->filesz > 0 && m->init != NULL) {
            memcpy(block, m->init, m->filesz);
        }
    }
}

static int dl_tls_pool_create(thread_tls_t* entry)
{
    int32_t tid = (int32_t)syscall0(SYSCALL_GETTID);
    entry->tid = tid;

    uint64_t total = dl_tls_pool_total();
    uint64_t align = g_tls_max_align;
    if (align < 16) align = 16;
    void* raw = malloc(total + align + 0x10u);
    if (raw == NULL) return -1;
    uint64_t pool = dl_align_up((uint64_t)(uintptr_t)raw, align);
    uint64_t tp = pool + total;

    memset((void*)(uintptr_t)pool, 0, total);
    dl_tls_pool_fill(pool, tp);

    *(uint64_t*)(uintptr_t)tp = tp;
    dl_tls_set_fs(tp);

    entry->raw = raw;
    entry->tp = tp;
    entry->total = total;
    return 0;
}

static int dl_tls_pool_extend(thread_tls_t* entry)
{
    uint64_t total = dl_tls_pool_total();
    if (total <= entry->total) return 0;
    uint64_t align = g_tls_max_align;
    if (align < 16) align = 16;
    uint64_t old_total = entry->total;
    uint64_t old_tp = entry->tp;
    uint64_t old_pool = old_tp - old_total;
    void* raw = malloc(total + align + 0x10u);
    if (raw == NULL) return -1;
    uint64_t pool = dl_align_up((uint64_t)(uintptr_t)raw, align);
    uint64_t tp = pool + total;

    memset((void*)(uintptr_t)pool, 0, total);
    dl_tls_pool_fill(pool, tp);

    /* Preserve live TLS values: existing module blocks keep their
     * TP-relative offsets, so the whole old pool region can be copied. */
    if (old_total > 0) {
        memcpy((void*)(uintptr_t)(tp - old_total),
               (void*)(uintptr_t)old_pool, old_total);
    }

    *(uint64_t*)(uintptr_t)tp = tp;
    dl_tls_set_fs(tp);

    free(entry->raw);
    entry->raw = raw;
    entry->tp = tp;
    entry->total = total;
    return 0;
}

/* Must be called with the dl lock held. */
static void dl_tls_ensure_pool_locked(void)
{
    if (g_tls_module_count == 0) return;
    int32_t tid = (int32_t)syscall0(SYSCALL_GETTID);
    thread_tls_t* entry = dl_tls_find_thread(tid);
    if (entry != NULL && entry->raw != NULL) {
        if (tid == g_tls_pool_owner_tid) {
            (void)dl_tls_pool_extend(entry);
        }
        return;
    }
    thread_tls_t* slot = NULL;
    for (int i = 0; i < DL_MAX_THREAD_TLS; i++) {
        if (g_thread_tls[i].tid == 0 && g_thread_tls[i].raw == NULL) {
            slot = &g_thread_tls[i];
            break;
        }
    }
    if (slot == NULL) return;
    (void)dl_tls_pool_create(slot);
    if (g_tls_pool_owner_tid == 0) {
        g_tls_pool_owner_tid = slot->tid;
    } else {
        g_tls_layout_finalized = 1;
    }
}

static thread_tls_t* dl_tls_find_thread(int32_t tid)
{
    for (int i = 0; i < DL_MAX_THREAD_TLS; i++) {
        if (g_thread_tls[i].tid == tid) return &g_thread_tls[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Dynamic relocation processing.                                     */
/* ------------------------------------------------------------------ */

static int dl_apply_rela_list(dso_t* d, const Elf64_Rela* rela,
    uint64_t count)
{
    for (uint64_t i = 0; i < count; i++) {
        const Elf64_Rela* r = &rela[i];
        uint64_t type = ELF64_R_TYPE(r->r_info);
        uint64_t sym_index = ELF64_R_SYM(r->r_info);
        uint64_t* place =
            (uint64_t*)(uintptr_t)(d->bias + r->r_offset);

        if (type == R_X86_64_NONE) continue;

        if (type == R_X86_64_RELATIVE) {
            *place = d->bias + (uint64_t)r->r_addend;
            continue;
        }

        if (type == R_X86_64_TLSGD || type == R_X86_64_TLSLD) {
            /* Link-time translation is already position independent. */
            continue;
        }

        if (type == R_X86_64_GOTPCREL || type == R_X86_64_GOTPCRELX ||
            type == R_X86_64_REX_GOTPCRELX) {
            *place = (uint64_t)r->r_addend;
            continue;
        }

        const Elf64_Sym* sym = dl_dynsym_at(d, sym_index);
        if (sym == NULL) {
            char nb[160];
            (void)snprintf(nb, sizeof(nb),
                "dynsym oob: idx=%llu off=%llx symtab=%p map=%p size=%llx",
                (unsigned long long)sym_index, (unsigned long long)r->r_offset,
                (const void*)d->symtab, (const void*)d->map_addr,
                (unsigned long long)d->map_size);
            dl_dbg(nb);
            set_dlerror("relocation symbol index out of range");
            return -1;
        }
        const char* sym_name =
            (sym->st_name < d->strsz) ? d->strtab + sym->st_name : "";
        dso_t* def_dso = NULL;
        const Elf64_Sym* def = NULL;
        if (sym->st_shndx != SHN_UNDEF) {
            def = sym;
            def_dso = d;
        } else {
            def = dl_lookup(sym_name, d, &def_dso);
        }
        if (type == R_X86_64_DTPMOD64) {
            if (sym_index == 0 || def == NULL) {
                *place = d->modid;
            } else {
                *place = def_dso->modid;
            }
            continue;
        }
        if (def == NULL && !(sym_index == 0 &&
                (type == R_X86_64_DTPOFF64 || type == R_X86_64_DTPOFF32 ||
                 type == R_X86_64_TPOFF64 || type == R_X86_64_TPOFF32 ||
                 type == R_X86_64_GOTTPOFF))) {
            char buf[192];
            dl_dbg("reloc lookup failed");
            dl_dbg(d->name);
            dl_dbg(sym_name);
            {
                char nb[160];
                (void)snprintf(nb, sizeof(nb),
                    "scope=%d symcount=%u shndx=%x strsz=%u strtab=%p hash=%p",
                    g_scope_count, (unsigned)g_linker_dso.sym_count,
                    (unsigned)g_linker_syms[0].st_shndx,
                    (unsigned)g_linker_dso.strsz, (const void*)g_linker_dso.strtab,
                    (const void*)g_linker_dso.hash);
                dl_dbg(nb);
            }
            (void)snprintf(buf, sizeof(buf),
                "undefined symbol '%s' referenced by %s", sym_name, d->name);
            set_dlerror(buf);
            return -1;
        }

        uint64_t value = 0;
        switch (type) {
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT:
            value = dl_symbol_value(def_dso, def);
            *place = value;
            break;

        case R_X86_64_64:
            value = dl_symbol_value(def_dso, def) + (uint64_t)r->r_addend;
            *place = value;
            break;

        case R_X86_64_PC32:
        case R_X86_64_PLT32:
            value = dl_symbol_value(def_dso, def) + (uint64_t)r->r_addend -
                (d->bias + r->r_offset);
            *(uint32_t*)place = (uint32_t)value;
            break;

        case R_X86_64_32:
        case R_X86_64_32S:
            value = dl_symbol_value(def_dso, def) + (uint64_t)r->r_addend;
            *(uint32_t*)place = (uint32_t)value;
            break;

        case R_X86_64_TPOFF64:
            value = (uint64_t)((def != NULL) ? def_dso : d)->tls_rel_tp +
                ((def != NULL) ? def->st_value : 0) +
                (uint64_t)r->r_addend;
            *place = value;
            break;

        case R_X86_64_TPOFF32:
        case R_X86_64_GOTTPOFF:
            value = (uint64_t)((def != NULL) ? def_dso : d)->tls_rel_tp +
                ((def != NULL) ? def->st_value : 0) +
                (uint64_t)r->r_addend;
            *(uint32_t*)place = (uint32_t)value;
            break;

        case R_X86_64_DTPMOD64:
            *place = (def != NULL) ? def_dso->modid : 0;
            break;

        case R_X86_64_DTPOFF64:
            *place = ((def != NULL) ? def->st_value : 0) +
                (uint64_t)r->r_addend;
            break;

        case R_X86_64_DTPOFF32:
            *(uint32_t*)place =
                (uint32_t)(((def != NULL) ? def->st_value : 0) +
                (uint64_t)r->r_addend);
            break;

        default: {
            char buf[192];
            (void)snprintf(buf, sizeof(buf),
                "unsupported relocation type %llu in %s",
                (unsigned long long)type, d->name);
            set_dlerror(buf);
            return -1;
        }
        }
    }
    return 0;
}

static int dl_apply_relocations(dso_t* d)
{
    if (d->rela != NULL && d->rela_count > 0) {
        uint64_t relative_count = d->rela_count;
        for (uint64_t i = 0; i < d->rela_count; i++) {
            if (ELF64_R_TYPE(d->rela[i].r_info) != R_X86_64_RELATIVE) {
                relative_count = i;
                break;
            }
        }
        if (relative_count > 0 &&
            dl_apply_rela_list(d, d->rela, relative_count) < 0) {
            return -1;
        }
        if (relative_count < d->rela_count &&
            dl_apply_rela_list(d, d->rela + relative_count,
                d->rela_count - relative_count) < 0) {
            return -1;
        }
    }
    if (d->jmp_rela != NULL && d->jmp_relasz > 0) {
        if (dl_apply_rela_list(d, d->jmp_rela, d->jmp_relasz) < 0) {
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Segment protection.                                                */
/* ------------------------------------------------------------------ */

static int dl_apply_segment_protection(dso_t* d, const Elf64_Phdr* loads,
    int load_count, uint64_t min_vaddr)
{
    for (int i = 0; i < load_count; i++) {
        const Elf64_Phdr* p = &loads[i];
        int prot = 0;
        if ((p->p_flags & PF_R) != 0) prot |= PROT_READ;
        if ((p->p_flags & PF_W) != 0) prot |= PROT_WRITE;
        if ((p->p_flags & PF_X) != 0) prot |= PROT_EXEC;
        if (prot == 0) continue;
        uint64_t start = dl_align_down(p->p_vaddr, PAGE_SIZE);
        uint64_t end = dl_align_up(p->p_vaddr + p->p_memsz, PAGE_SIZE);
        if (end == start) continue;
        uint64_t min_aligned = dl_align_down(min_vaddr, PAGE_SIZE);
        void* addr = (void*)(uintptr_t)(
            (uint64_t)(uintptr_t)d->map_addr + (start - min_aligned));
        if (mprotect(addr, end - start, prot) < 0) {
            set_dlerror("mprotect failed while loading shared object");
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Shared object loading.                                             */
/* ------------------------------------------------------------------ */

static int dl_dso_exists(const char* path, dso_t** existing)
{
    for (int i = 0; i < g_dso_count; i++) {
        dso_t* d = &g_dsos[i];
        if (d->name[0] == '\0') continue;
        if (strcmp(d->name, path) == 0 ||
            (d->soname[0] != '\0' && strcmp(d->soname, path) == 0)) {
            *existing = d;
            return 1;
        }
    }
    return 0;
}

static void dl_parse_dynamic(dso_t* d)
{
    if (d->dyn == NULL) return;
    uint64_t i = 0;
    while (d->dyn[i].d_tag != DT_NULL) {
        uint64_t tag = d->dyn[i].d_tag;
        uint64_t value = d->dyn[i].d_un.d_val;
        uint64_t ptr = d->bias + d->dyn[i].d_un.d_ptr;
        switch (tag) {
        case DT_STRTAB: d->strtab = (const char*)(uintptr_t)ptr; break;
        case DT_SYMTAB: d->symtab = (const Elf64_Sym*)(uintptr_t)ptr; break;
        case DT_STRSZ: d->strsz = value; break;
        case DT_SYMENT: d->syment = value; break;
        case DT_HASH: d->hash = (const uint32_t*)(uintptr_t)ptr; break;
        case DT_GNU_HASH:
            d->gnu_hash = (const uint32_t*)(uintptr_t)ptr;
            break;
        case DT_RELA: d->rela = (const Elf64_Rela*)(uintptr_t)ptr; break;
        case DT_RELASZ: d->relasz = value; break;
        case DT_RELACOUNT:
            if (d->rela != NULL && value <= d->relasz) {
                d->rela_count = value;
            }
            break;
        case DT_JMPREL:
            d->jmp_rela = (const Elf64_Rela*)(uintptr_t)ptr;
            break;
        case DT_PLTRELSZ: d->jmp_relasz = value; break;
        case DT_INIT: d->init_fn = (void (*)(void))(uintptr_t)ptr; break;
        case DT_FINI: d->fini_fn = (void (*)(void))(uintptr_t)ptr; break;
        case DT_INIT_ARRAY:
            d->init_array = (void**)(uintptr_t)ptr;
            break;
        case DT_INIT_ARRAYSZ: d->init_array_sz = value; break;
        case DT_FINI_ARRAY:
            d->fini_array = (void**)(uintptr_t)ptr;
            break;
        case DT_FINI_ARRAYSZ: d->fini_array_sz = value; break;
        case DT_SONAME:
            if (value < d->strsz && d->strtab != NULL) {
                (void)snprintf(d->soname, sizeof(d->soname), "%s",
                    d->strtab + value);
            }
            break;
        default:
            break;
        }
        i++;
        if (i > 4096) break;
    }
}

static int dl_resolve_needed_path(const char* needed, const char* dependee,
    char* out, size_t out_size)
{
    if (needed[0] == '/') {
        (void)snprintf(out, out_size, "%s", needed);
        return 1;
    }
    if (dependee != NULL && strchr(dependee, '/') != NULL) {
        const char* slash = strrchr(dependee, '/');
        size_t dir_len = (size_t)(slash - dependee);
        if (dir_len + 1 + strlen(needed) + 1 < out_size) {
            memcpy(out, dependee, dir_len);
            out[dir_len] = '/';
            strcpy(out + dir_len + 1, needed);
            return 1;
        }
    }
    const char* ld_library_path = getenv("LD_LIBRARY_PATH");
    if (ld_library_path != NULL && ld_library_path[0] != '\0') {
        const char* start = ld_library_path;
        while (*start != '\0') {
            const char* end = start;
            while (*end != '\0' && *end != ':') end++;
            size_t len = (size_t)(end - start);
            if (len + 1 + strlen(needed) + 1 < out_size) {
                memcpy(out, start, len);
                out[len] = '/';
                strcpy(out + len + 1, needed);
                return 1;
            }
            start = (*end == ':') ? end + 1 : end;
        }
    }
    (void)snprintf(out, out_size, "/Userland/Library/%s", needed);
    return 1;
}

static void dl_run_init(dso_t* d)
{
    for (uint64_t i = 0; i < d->init_array_sz; i++) {
        void (*fn)(void) = (void (*)(void))(uintptr_t)d->init_array[i];
        if (fn != NULL) fn();
    }
    if (d->init_fn != NULL) d->init_fn();
}

static void dl_run_fini(dso_t* d)
{
    if (d->fini_fn != NULL) d->fini_fn();
    for (uint64_t i = d->fini_array_sz; i > 0; i--) {
        void (*fn)(void) = (void (*)(void))(uintptr_t)d->fini_array[i - 1];
        if (fn != NULL) fn();
    }
}

static int dl_load_one(const char* path, int flags, dso_t** out,
    dso_t** loaded_list, int* loaded_count)
{
    dso_t* existing = NULL;
    if (dl_dso_exists(path, &existing)) {
        existing->refcount++;
        if (((uint32_t)flags & RTLD_GLOBAL) != 0 && !existing->global) {
            existing->global = 1;
            if (g_scope_count < DL_MAX_DSOS) {
                g_scope[g_scope_count++] = existing;
            }
        }
        if (((uint32_t)flags & RTLD_NODELETE) != 0) {
            existing->keep = 1;
        }
        *out = existing;
        return 0;
    }

    uint64_t file_size = 0;
    uint8_t* file = dl_read_whole_file(path, &file_size);
    if (file == NULL) {
        char buf[192];
        (void)snprintf(buf, sizeof(buf),
            "cannot open shared object file: %s", path);
        set_dlerror(buf);
        return -1;
    }
    if (file_size < sizeof(Elf64_Ehdr)) {
        free(file);
        set_dlerror("truncated ELF header");
        return -1;
    }

    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)(const void*)file;
    if (memcmp(ehdr->e_ident, "\177ELF", 4) != 0 ||
        ehdr->e_ident[4] != ELFCLASS64 || ehdr->e_ident[5] != ELFDATA2LSB ||
        ehdr->e_type != ET_DYN || ehdr->e_machine != EM_X86_64) {
        free(file);
        set_dlerror("not an x86-64 shared object");
        return -1;
    }
    if (ehdr->e_phentsize < sizeof(Elf64_Phdr) || ehdr->e_phnum == 0) {
        free(file);
        set_dlerror("bad program header table");
        return -1;
    }
    uint64_t phoff = ehdr->e_phoff;
    uint64_t phnum = ehdr->e_phnum;
    if (phoff + phnum * ehdr->e_phentsize > file_size) {
        free(file);
        set_dlerror("program header table out of range");
        return -1;
    }

    const Elf64_Phdr* phdr_table =
        (const Elf64_Phdr*)(const void*)(file + phoff);
    Elf64_Phdr loads[16];
    int load_count = 0;
    const Elf64_Phdr* dyn_phdr = NULL;
    const Elf64_Phdr* tls_phdr = NULL;
    uint64_t min_vaddr = UINT64_MAX;
    uint64_t max_end = 0;

    for (uint64_t i = 0; i < phnum; i++) {
        const Elf64_Phdr* p = (const Elf64_Phdr*)(const void*)
            ((const uint8_t*)phdr_table + i * ehdr->e_phentsize);
        if (p->p_type == PT_LOAD && load_count < 16) {
            loads[load_count++] = *p;
            if (p->p_vaddr < min_vaddr) min_vaddr = p->p_vaddr;
            uint64_t end = p->p_vaddr + p->p_memsz;
            if (end > max_end) max_end = end;
        } else if (p->p_type == PT_DYNAMIC) {
            dyn_phdr = p;
        } else if (p->p_type == PT_TLS) {
            tls_phdr = p;
        }
    }
    if (load_count == 0 || min_vaddr == UINT64_MAX) {
        free(file);
        set_dlerror("shared object has no loadable segments");
        return -1;
    }
    if (dyn_phdr == NULL) {
        free(file);
        set_dlerror("shared object has no dynamic section");
        return -1;
    }

    uint64_t min_aligned = dl_align_down(min_vaddr, PAGE_SIZE);
    uint64_t max_aligned = dl_align_up(max_end, PAGE_SIZE);
    uint64_t image_size = max_aligned - min_aligned;
    if (image_size == 0) image_size = PAGE_SIZE;

    dso_t* d = NULL;
    for (int i = 0; i < DL_MAX_DSOS; i++) {
        if (g_dsos[i].name[0] == '\0') {
            d = &g_dsos[i];
            break;
        }
    }
    if (d == NULL) {
        free(file);
        set_dlerror("too many shared objects loaded");
        return -1;
    }

    uint8_t* map_addr = (uint8_t*)mmap(NULL, image_size,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map_addr == MAP_FAILED) {
        free(file);
        set_dlerror("mmap failed while loading shared object");
        return -1;
    }
    uint64_t bias = (uint64_t)(uintptr_t)map_addr - min_aligned;

    for (int i = 0; i < load_count; i++) {
        const Elf64_Phdr* p = &loads[i];
        if (p->p_offset + p->p_filesz > file_size) {
            munmap(map_addr, image_size);
            free(file);
            set_dlerror("segment data out of range");
            return -1;
        }
        uint8_t* dst = map_addr + (p->p_vaddr - min_aligned);
        if (p->p_filesz > 0) {
            memcpy(dst, file + p->p_offset, p->p_filesz);
        }
    }

    memset(d, 0, sizeof(*d));
    (void)snprintf(d->name, sizeof(d->name), "%s", path);
    d->map_addr = map_addr;
    d->map_size = image_size;
    d->bias = bias;
    d->dyn = (const Elf64_Dyn*)(uintptr_t)(bias + dyn_phdr->p_vaddr);

    dl_parse_dynamic(d);

    if (d->strtab == NULL || d->symtab == NULL || d->strsz == 0) {
        munmap(map_addr, image_size);
        free(file);
        memset(d, 0, sizeof(*d));
        set_dlerror("shared object lacks symbol tables");
        return -1;
    }
    if (d->syment < sizeof(Elf64_Sym)) {
        munmap(map_addr, image_size);
        free(file);
        memset(d, 0, sizeof(*d));
        set_dlerror("invalid symbol entry size");
        return -1;
    }

    if (d->hash != NULL) {
        d->sym_count = d->hash[1];
    } else if (d->gnu_hash != NULL) {
        uint32_t symoffset = d->gnu_hash[1];
        d->sym_count = symoffset;
        const uint32_t* buckets = d->gnu_hash + 4 + d->gnu_hash[2];
        uint32_t nbuckets = d->gnu_hash[0];
        const uint32_t* chain = buckets + nbuckets;
        uint32_t limit = 0;
        for (uint32_t i = 0; i < nbuckets; i++) {
            if (buckets[i] != 0) limit++;
        }
        for (uint32_t i = 0; i < 200000u && limit != 0; i++) {
            if ((chain[i] & 1u) != 0) limit--;
            d->sym_count++;
        }
    } else {
        uint64_t max_syms = d->relasz + d->strsz;
        d->sym_count = (uint32_t)(max_syms / d->syment);
        if (d->sym_count > 200000u) d->sym_count = 200000u;
    }

    if (d->rela != NULL && d->rela_count == 0) {
        d->rela_count = d->relasz / sizeof(Elf64_Rela);
    }
    if (d->jmp_rela != NULL) {
        d->jmp_relasz /= sizeof(Elf64_Rela);
    }
    if (d->rela != NULL) {
        d->relasz /= sizeof(Elf64_Rela);
    }

    if (tls_phdr != NULL) {
        d->tls_align = tls_phdr->p_align;
        d->tls_filesz = tls_phdr->p_filesz;
        d->tls_memsz = tls_phdr->p_memsz;
        if (tls_phdr->p_filesz > 0) {
            d->tls_init = file + tls_phdr->p_offset;
        }
    }

    uint64_t tls_cursor_before = g_tls_dso_cursor;
    int tls_registered = 0;

    /* Resolve DT_NEEDED dependencies (registration first for cycles). */
    for (uint64_t i = 0; i < 4096; i++) {
        const Elf64_Dyn* tag = &d->dyn[i];
        if (tag->d_tag == DT_NULL) break;
        if (tag->d_tag != DT_NEEDED) continue;
        if (d->needed_count >= DL_MAX_NEEDED) break;
        if (tag->d_un.d_val >= d->strsz) continue;
        const char* needed = d->strtab + tag->d_un.d_val;
        if (needed[0] == '\0') continue;
        char needed_path[256];
        (void)dl_resolve_needed_path(needed, d->name, needed_path,
            sizeof(needed_path));
        dso_t* dep = NULL;
        if (dl_load_one(needed_path, flags, &dep, loaded_list,
                loaded_count) < 0) {
            if (tls_registered) {
                g_tls_module_count--;
                g_tls_dso_cursor = tls_cursor_before;
            }
            if (d->tls_storage != NULL) free(d->tls_storage);
            munmap(map_addr, image_size);
            free(file);
            memset(d, 0, sizeof(*d));
            return -1;
        }
        d->needed[d->needed_count++] = dep;
    }

    /* TLS layout slot. */
    if (tls_phdr != NULL) {
        if (dl_tls_register_dso(d) < 0) {
            munmap(map_addr, image_size);
            free(file);
            memset(d, 0, sizeof(*d));
            set_dlerror("cannot load TLS shared object: "
                "TLS layout already finalized (load it before threads)");
            return -1;
        }
        tls_registered = 1;
        if (d->tls_filesz > 0 && d->tls_init != NULL) {
            void* storage = malloc(d->tls_filesz);
            if (storage == NULL) {
                g_tls_module_count--;
                g_tls_dso_cursor = tls_cursor_before;
                munmap(map_addr, image_size);
                free(file);
                memset(d, 0, sizeof(*d));
                set_dlerror("out of memory for TLS initial image");
                return -1;
            }
            memcpy(storage, d->tls_init, d->tls_filesz);
            d->tls_init = storage;
            d->tls_storage = storage;
        }
    }

    /* Bind relocations. */
    if (dl_apply_relocations(d) < 0) {
        if (tls_registered) {
            g_tls_module_count--;
            g_tls_dso_cursor = tls_cursor_before;
        }
        if (d->tls_storage != NULL) free(d->tls_storage);
        munmap(map_addr, image_size);
        free(file);
        memset(d, 0, sizeof(*d));
        return -1;
    }

    /* Protect segments per program header. */
    if (dl_apply_segment_protection(d, loads, load_count, min_vaddr) < 0) {
        if (tls_registered) {
            g_tls_module_count--;
            g_tls_dso_cursor = tls_cursor_before;
        }
        if (d->tls_storage != NULL) free(d->tls_storage);
        munmap(map_addr, image_size);
        free(file);
        memset(d, 0, sizeof(*d));
        return -1;
    }

    free(file);

    d->refcount = 1;
    d->keep = ((uint32_t)flags & RTLD_NODELETE) != 0;
    if (((uint32_t)flags & RTLD_GLOBAL) != 0) {
        d->global = 1;
        if (g_scope_count < DL_MAX_DSOS) {
            g_scope[g_scope_count++] = d;
        }
    }
    g_dso_count++;
    if (loaded_list != NULL && loaded_count != NULL &&
        *loaded_count < DL_MAX_DSOS) {
        loaded_list[(*loaded_count)++] = d;
    }
    *out = d;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API.                                                        */
/* ------------------------------------------------------------------ */

void* dlopen(const char* filename, int flags)
{
    dl_ensure_init();

    uint32_t known = (uint32_t)(RTLD_LAZY | RTLD_NOW | RTLD_BINDING_MASK |
        RTLD_NOLOAD | RTLD_DEEPBIND | RTLD_GLOBAL | RTLD_LOCAL |
        RTLD_NODELETE);
    if (((uint32_t)flags & ~known) != 0) {
        set_dlerror("invalid dlopen flags");
        return NULL;
    }
    if (filename == NULL) {
        return &g_main_dso;
    }

    dl_lock();
    if (!g_main_registered) {
        (void)dl_tls_register_main();
        g_main_registered = 1;
    }

    dso_t* result = NULL;
    dso_t* loaded[DL_MAX_DSOS];
    int loaded_count = 0;

    if (((uint32_t)flags & RTLD_NOLOAD) != 0) {
        dso_t* existing = NULL;
        if (dl_dso_exists(filename, &existing)) {
            existing->refcount++;
            result = existing;
        }
    } else {
        int status = dl_load_one(filename, flags, &result, loaded,
            &loaded_count);
        if (status < 0) {
            dl_unlock();
            return NULL;
        }
    }

    if (result != NULL) {
        dl_tls_ensure_pool_locked();
    }

    dl_unlock();

    if (result == NULL) {
        set_dlerror("cannot open shared object file (RTLD_NOLOAD)");
        return NULL;
    }

    /* Run initializers outside the lock (they may call dlopen). */
    for (int i = 0; i < loaded_count; i++) {
        dl_run_init(loaded[i]);
    }

    g_have_error = 0;
    return result;
}

void* dlsym(void* handle, const char* symbol)
{
    dl_ensure_init();

    if (symbol == NULL || symbol[0] == '\0') {
        set_dlerror("invalid symbol name");
        return NULL;
    }

    dl_lock();

    dso_t* root = NULL;
    if (handle == RTLD_DEFAULT) {
        root = NULL;
    } else if (handle == RTLD_NEXT) {
        dl_unlock();
        set_dlerror("RTLD_NEXT is not supported");
        return NULL;
    } else {
        root = (dso_t*)handle;
        int valid = (root == &g_main_dso);
        if (!valid) {
            for (int i = 0; i < g_dso_count; i++) {
                if (root == &g_dsos[i] && g_dsos[i].name[0] != '\0') {
                    valid = 1;
                    break;
                }
            }
        }
        if (!valid) {
            dl_unlock();
            set_dlerror("invalid handle passed to dlsym");
            return NULL;
        }
    }

    dso_t* def = NULL;
    const Elf64_Sym* sym = NULL;
    if (root == NULL) {
        for (int i = 0; i < g_scope_count; i++) {
            sym = dl_lookup_in_dso(g_scope[i], symbol);
            if (sym != NULL) {
                def = g_scope[i];
                break;
            }
        }
    } else {
        sym = dl_lookup_local_group(root, symbol, &def);
        if (sym == NULL) {
            for (int i = 0; i < g_scope_count; i++) {
                sym = dl_lookup_in_dso(g_scope[i], symbol);
                if (sym != NULL) {
                    def = g_scope[i];
                    break;
                }
            }
        }
    }
    if (sym == NULL) {
        dl_unlock();
        set_dlerror("symbol not found");
        return NULL;
    }

    void* address;
    if (ELF64_ST_TYPE(sym->st_info) == STT_TLS) {
        dl_tls_ensure_pool_locked();
        uint64_t tp = dl_get_tp();
        address = (void*)(uintptr_t)(
            tp + (uint64_t)def->tls_rel_tp + sym->st_value);
    } else {
        address = (void*)(uintptr_t)dl_symbol_value(def, sym);
    }

    g_have_error = 0;
    dl_unlock();
    return address;
}

int dlclose(void* handle)
{
    dl_ensure_init();

    dl_lock();

    if (handle == NULL || handle == RTLD_DEFAULT || handle == RTLD_NEXT) {
        dl_unlock();
        set_dlerror("invalid handle passed to dlclose");
        return -1;
    }

    dso_t* d = (dso_t*)handle;
    if (d == &g_main_dso) {
        dl_unlock();
        return 0;
    }
    int valid = 0;
    for (int i = 0; i < g_dso_count; i++) {
        if (d == &g_dsos[i] && g_dsos[i].name[0] != '\0') {
            valid = 1;
            break;
        }
    }
    if (!valid || d->refcount == 0) {
        dl_unlock();
        set_dlerror("invalid handle passed to dlclose");
        return -1;
    }

    dso_t* unload[DL_MAX_DSOS];
    int unload_count = 0;

    d->refcount--;
    if (d->refcount == 0 && !d->keep && d->modid == 0 && !d->global) {
        for (uint32_t i = 0; i < d->needed_count; i++) {
            if (d->needed[i] != NULL && d->needed[i]->refcount > 0) {
                d->needed[i]->refcount--;
            }
        }
        d->name[0] = '\0';
        unload[unload_count++] = d;

        for (int pass = 0; pass < 8; pass++) {
            int changed = 0;
            for (int i = 0; i < g_dso_count; i++) {
                dso_t* cand = &g_dsos[i];
                if (cand->name[0] == '\0' || cand->is_main) continue;
                if (cand->refcount != 0 || cand->keep || cand->modid != 0 ||
                    cand->global) {
                    continue;
                }
                for (uint32_t j = 0; j < cand->needed_count; j++) {
                    if (cand->needed[j] != NULL &&
                        cand->needed[j]->refcount > 0) {
                        cand->needed[j]->refcount--;
                    }
                }
                cand->name[0] = '\0';
                unload[unload_count++] = cand;
                changed = 1;
            }
            if (!changed) break;
        }
    }

    dl_unlock();

    if (unload_count > 0) {
        for (int i = unload_count - 1; i >= 0; i--) {
            dl_run_fini(unload[i]);
        }
        for (int i = 0; i < unload_count; i++) {
            dso_t* victim = unload[i];
            if (victim->tls_storage != NULL) free(victim->tls_storage);
            (void)munmap(victim->map_addr, victim->map_size);
            memset(victim, 0, sizeof(*victim));
        }
    }

    g_have_error = 0;
    return 0;
}

char* dlerror(void)
{
    dl_ensure_init();

    char* result = NULL;
    dl_lock();
    if (g_have_error) {
        g_have_error = 0;
        result = g_error_buf;
    }
    dl_unlock();
    return result;
}

/* ------------------------------------------------------------------ */
/* TLS support.                                                       */
/* ------------------------------------------------------------------ */

static void* dl_tls_get_addr(tls_index* ti)
{
    dl_lock();
    dl_tls_ensure_pool_locked();
    dl_unlock();

    uint64_t tp = dl_get_tp();
    uint64_t module = ti->ti_module;
    if (module == 0 || module > (uint64_t)g_tls_module_count) {
        return (void*)(uintptr_t)tp;
    }
    const dl_tls_module_t* m = &g_tls_modules[module - 1];
    return (void*)(uintptr_t)(tp + (uint64_t)m->rel_tp + ti->ti_offset);
}

void* __tls_get_addr(tls_index* ti)
{
    return dl_tls_get_addr(ti);
}

/* ------------------------------------------------------------------ */
/* Thread hooks (called by the pthread implementation).               */
/* ------------------------------------------------------------------ */

void implus_dl_thread_init(void)
{
    dl_ensure_init();

    dl_lock();

    if (!g_main_registered) {
        (void)dl_tls_register_main();
        g_main_registered = 1;
    }

    dl_tls_ensure_pool_locked();

    dl_unlock();
}

void implus_dl_thread_cleanup(void)
{
    dl_ensure_init();

    int32_t tid = (int32_t)syscall0(SYSCALL_GETTID);
    dl_lock();
    thread_tls_t* entry = dl_tls_find_thread(tid);
    if (entry != NULL && entry->raw != NULL) {
        free(entry->raw);
        memset(entry, 0, sizeof(*entry));
    }
    dl_unlock();
}

/* ------------------------------------------------------------------ */
/* Linker pseudo-DSO and one-time initialization.                     */
/* ------------------------------------------------------------------ */

static void dl_init(void)
{
    if (g_initialized) return;

    memset(&g_linker_dso, 0, sizeof(g_linker_dso));
    (void)snprintf(g_linker_dso.name, sizeof(g_linker_dso.name),
        "ld-implus.so");
    g_linker_dso.strtab = g_linker_strtab;
    g_linker_dso.symtab = g_linker_syms;
    g_linker_dso.strsz = sizeof(g_linker_strtab);
    g_linker_dso.syment = sizeof(Elf64_Sym);
    g_linker_dso.sym_count = 1;

    memset(&g_main_dso, 0, sizeof(g_main_dso));
    (void)snprintf(g_main_dso.name, sizeof(g_main_dso.name), "main");
    g_main_dso.is_main = 1;

    g_initialized = 1;
}

static void dl_ensure_init(void)
{
    if (!g_initialized) dl_init();
}

__attribute__((constructor)) static void dl_auto_init(void)
{
    dl_init();
}
