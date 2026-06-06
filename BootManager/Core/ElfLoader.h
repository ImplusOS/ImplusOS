#ifndef IMPLUSOS_BOOT_ELF_LOADER_H
#define IMPLUSOS_BOOT_ELF_LOADER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    BOOT_ELF_ARCH_AUTO,
    BOOT_ELF_ARCH_X86_64,
    BOOT_ELF_ARCH_AARCH64
} boot_elf_arch_t;

typedef struct {
    void *image;
    size_t image_size;
    boot_elf_arch_t arch;
    int dynamic_anywhere;
    void *(*alloc_pages_at)(uint64_t address, size_t pages, void *ctx);
    void *(*alloc_pages_any)(size_t pages, uint64_t *phys_out, void *ctx);
    void (*free_pages)(uint64_t address, size_t pages, void *ctx);
    void *ctx;
} boot_elf_load_request_t;

typedef struct {
    uint64_t base;
    uint64_t span;
    uint64_t entry;
} boot_elf_load_result_t;

int boot_elf_load64(const boot_elf_load_request_t *req, boot_elf_load_result_t *out);

#endif
