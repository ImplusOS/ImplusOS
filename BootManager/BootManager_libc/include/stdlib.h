#ifndef BOOTMANAGER_LIBC_STDLIB_H
#define BOOTMANAGER_LIBC_STDLIB_H

#include <stddef.h>
#include "../../EDK2Compat.h"

void bootmanager_libc_init(EFI_SYSTEM_TABLE *system_table);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);

#endif

