#include "../include/stdlib.h"
#include "../include/string.h"
#include <efilib.h>

static EFI_SYSTEM_TABLE *g_system_table;

void bootmanager_libc_init(EFI_SYSTEM_TABLE *system_table) {
    g_system_table = system_table;
}

void *malloc(size_t size) {
    if (!g_system_table || size == 0) return NULL;
    void *ptr = NULL;
    EFI_STATUS status = uefi_call_wrapper(
        g_system_table->BootServices->AllocatePool, 3,
        EfiLoaderData, (UINTN)size, &ptr);
    return EFI_ERROR(status) ? NULL : ptr;
}

void free(void *ptr) {
    if (!g_system_table || !ptr) return;
    uefi_call_wrapper(g_system_table->BootServices->FreePool, 1, ptr);
}

void *calloc(size_t count, size_t size) {
    if (size != 0 && count > ((size_t)-1) / size) return NULL;
    size_t total = count * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}
