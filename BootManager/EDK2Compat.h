#ifndef BOOTMANAGER_EDK2_COMPAT_H
#define BOOTMANAGER_EDK2_COMPAT_H

#include <Uefi.h>

#ifndef InitializeLib
#define InitializeLib(ImageHandle, SystemTable) \
    do { (void)(ImageHandle); (void)(SystemTable); } while (0)
#endif

#ifndef uefi_call_wrapper
#define uefi_call_wrapper(func, nargs, ...) func(__VA_ARGS__)
#endif

#endif
