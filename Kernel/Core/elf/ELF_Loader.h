#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t max_file_size;
    uint64_t min_vaddr;
    uint64_t max_vaddr;
} elf_load_policy_t;

typedef struct {
    uint64_t max_file_size;
    uint64_t max_image_size;
    uint32_t max_relocation_count;
} elf_module_load_policy_t;

typedef struct {
    void *load_base;
    uint32_t page_count;
    uint64_t image_span;
    uint64_t entry;
} elf_loaded_module_t;

typedef struct {
    uint64_t entry;
    uint64_t phdr_vaddr;
    uint64_t phent;
    uint64_t phnum;
} elf_loaded_image_info_t;

bool elf_loader_load_from_path(uint64_t target_cr3,
                               const char *path,
                               const elf_load_policy_t *policy,
                               elf_loaded_image_info_t *image_out);
bool elf_loader_load_from_memory(uint64_t target_cr3,
                                 const void *file_data,
                                 uint64_t file_size,
                                 const elf_load_policy_t *policy,
                                 elf_loaded_image_info_t *image_out);
bool elf_loader_load_module_from_memory(const void *file_data,
                                        uint64_t file_size,
                                        const elf_module_load_policy_t *policy,
                                        uint64_t *entry_out);
bool elf_loader_load_module_image_from_memory(const void *file_data,
                                              uint64_t file_size,
                                              const elf_module_load_policy_t *policy,
                                              elf_loaded_module_t *module_out);
