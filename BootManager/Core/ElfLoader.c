#include "ElfLoader.h"

#include "../BootManager_libc/include/string.h"
#include "../ElfDefs.h"

#include <Library/UefiLib.h>

#define ELF_PAGE_SIZE 4096ULL

static uint64_t align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1u);
}

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1u) & ~(align - 1u);
}

static uint32_t rela_type(uint64_t info)
{
    return (uint32_t)(info & 0xFFFFFFFFu);
}

static int arch_accepts_machine(boot_elf_arch_t arch, uint16_t machine)
{
    if (arch == BOOT_ELF_ARCH_AUTO)
        return (machine == EM_X86_64 || machine == EM_AARCH64);
    if (arch == BOOT_ELF_ARCH_X86_64)  return machine == EM_X86_64;
    if (arch == BOOT_ELF_ARCH_AARCH64) return machine == EM_AARCH64;
    return 0;
}

static int arch_relative_reloc(boot_elf_arch_t arch, uint32_t type, uint16_t machine)
{
    boot_elf_arch_t effective = arch;
    if (effective == BOOT_ELF_ARCH_AUTO) {
        if      (machine == EM_X86_64)   effective = BOOT_ELF_ARCH_X86_64;
        else if (machine == EM_AARCH64)  effective = BOOT_ELF_ARCH_AARCH64;
        else return 0;
    }
    if (effective == BOOT_ELF_ARCH_X86_64)  return type == R_X86_64_RELATIVE;
    if (effective == BOOT_ELF_ARCH_AARCH64) return type == R_AARCH64_RELATIVE;
    return 0;
}

int boot_elf_load64(const boot_elf_load_request_t *req, boot_elf_load_result_t *out)
{
    if (!req || !out || !req->image || !req->alloc_pages_at) {
        return -1;
    }
    if (req->image_size < sizeof(Elf64_Ehdr)) {
        return -1;
    }

    Elf64_Ehdr *eh = (Elf64_Ehdr *)req->image;


    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
        return -1;
    }
    if (eh->e_ident[4] != 2) {
        return -1;
    }
    if (!arch_accepts_machine(req->arch, eh->e_machine)) {
        return -1;
    }
    if (eh->e_phentsize != sizeof(Elf64_Phdr)) {
        return -1;
    }
    if (eh->e_phoff > req->image_size ||
        eh->e_phoff + ((uint64_t)eh->e_phnum * sizeof(Elf64_Phdr)) > req->image_size) {
        return -1;
    }

    Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)req->image + eh->e_phoff);
    uint64_t min = UINT64_MAX;
    uint64_t max = 0;
    uint16_t load_count = 0;

    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        uint64_t start = ph[i].p_vaddr;
        uint64_t end   = start + ph[i].p_memsz;
        if (end < start) {
            return -1;
        }
        if (start < min) min = start;
        if (end   > max) max = end;
        ++load_count;
    }

    if (load_count == 0 || max <= min) {
        return -1;
    }

    uint64_t aligned_min = align_down(min, ELF_PAGE_SIZE);
    uint64_t aligned_max = align_up(max, ELF_PAGE_SIZE);
    size_t   pages       = (size_t)((aligned_max - aligned_min) / ELF_PAGE_SIZE);

    uint64_t base      = aligned_min + req->load_slide;
    void    *load_base = req->alloc_pages_at(base, pages, req->ctx);

    if (!load_base && req->load_slide != 0) {
        uint64_t fallback_slide = (req->load_slide / 2) & ~0x1FFFFFULL;
        if (fallback_slide != 0 && fallback_slide != req->load_slide) {
            base = aligned_min + fallback_slide;
            load_base = req->alloc_pages_at(base, pages, req->ctx);
        }
        if (!load_base) {
            base = aligned_min;
            load_base = req->alloc_pages_at(base, pages, req->ctx);
        }
    }

    if (!load_base && eh->e_type == ET_DYN && req->dynamic_anywhere && req->alloc_pages_any) {
        load_base = req->alloc_pages_any(pages, &base, req->ctx);
    }
    if (!load_base) {
        return -1;
    }

    memset((void *)(uintptr_t)base, 0, pages * (size_t)ELF_PAGE_SIZE);

    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_offset > req->image_size ||
            ph[i].p_offset + ph[i].p_filesz > req->image_size ||
            ph[i].p_memsz < ph[i].p_filesz) {
            if (req->free_pages) req->free_pages(base, pages, req->ctx);
            return -1;
        }
        uint64_t dest = base + (ph[i].p_vaddr - aligned_min);
        memcpy((void *)(uintptr_t)dest,
               (uint8_t *)req->image + ph[i].p_offset,
               (size_t)ph[i].p_filesz);
    }

    uint64_t load_bias = base - aligned_min;
    if (eh->e_shoff != 0 && eh->e_shnum != 0) {
        if (eh->e_shentsize != sizeof(Elf64_Shdr) ||
            eh->e_shoff > req->image_size ||
            eh->e_shoff + ((uint64_t)eh->e_shnum * sizeof(Elf64_Shdr)) > req->image_size) {
            if (req->free_pages) req->free_pages(base, pages, req->ctx);
            return -1;
        }
        Elf64_Shdr *sh = (Elf64_Shdr *)((uint8_t *)req->image + eh->e_shoff);
        for (uint16_t i = 0; i < eh->e_shnum; ++i) {
            if (sh[i].sh_type != SHT_RELA) continue;
            if (sh[i].sh_offset > req->image_size ||
                sh[i].sh_offset + sh[i].sh_size > req->image_size) {
                if (req->free_pages) req->free_pages(base, pages, req->ctx);
                return -1;
            }
            Elf64_Rela *rela  = (Elf64_Rela *)((uint8_t *)req->image + sh[i].sh_offset);
            uint64_t    count = sh[i].sh_size / sizeof(Elf64_Rela);
            for (uint64_t j = 0; j < count; ++j) {
                if (!arch_relative_reloc(req->arch, rela_type(rela[j].r_info), eh->e_machine))
                    continue;
                uint64_t  target_addr = base + (rela[j].r_offset - aligned_min);
                uint64_t *target      = (uint64_t *)(uintptr_t)target_addr;
                *target = load_bias + (uint64_t)rela[j].r_addend;
            }
        }
    }

    out->base  = base;
    out->span  = pages * ELF_PAGE_SIZE;
    out->entry = base + (eh->e_entry - aligned_min);
    return 0;
}