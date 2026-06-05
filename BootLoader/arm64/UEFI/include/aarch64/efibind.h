#ifndef AARCH64_EFI_BIND
#define AARCH64_EFI_BIND

#include <stdint.h>
#include <stddef.h>

typedef uint64_t UINT64;
typedef int64_t  INT64;
typedef uint32_t UINT32;
typedef int32_t  INT32;
typedef uint16_t UINT16;
typedef int16_t  INT16;
typedef uint8_t  UINT8;
typedef int8_t   INT8;
typedef char     CHAR8;
typedef uint16_t CHAR16;
typedef void     VOID;
typedef int64_t  INTN;
typedef uint64_t UINTN;

#define WCHAR CHAR16
#define EFIERR(a)           (0x8000000000000000ULL | (a))
#define EFI_ERROR_MASK      0x8000000000000000ULL
#define EFIERR_OEM(a)       (0xc000000000000000ULL | (a))
#define BAD_POINTER         0xFBFBFBFBFBFBFBFBULL
#define MAX_ADDRESS         0xFFFFFFFFFFFFFFFFULL
#define BREAKPOINT()        while (1) { }
#define MIN_ALIGNMENT_SIZE  8
#define ALIGN_VARIABLE(Value, Adjustment) \
    (UINTN)(Adjustment) = 0; \
    if ((UINTN)(Value) % MIN_ALIGNMENT_SIZE) \
        (UINTN)(Adjustment) = MIN_ALIGNMENT_SIZE - ((UINTN)(Value) % MIN_ALIGNMENT_SIZE); \
    (Value) = (UINTN)(Value) + (UINTN)(Adjustment)

#define EFI_SIGNATURE_16(A,B)             ((A) | ((B) << 8))
#define EFI_SIGNATURE_32(A,B,C,D)         (EFI_SIGNATURE_16(A,B) | (EFI_SIGNATURE_16(C,D) << 16))
#define EFI_SIGNATURE_64(A,B,C,D,E,F,G,H) (EFI_SIGNATURE_32(A,B,C,D) | ((UINT64)(EFI_SIGNATURE_32(E,F,G,H)) << 32))

#ifndef EFIAPI
#define EFIAPI
#endif

#define BOOTSERVICE
#define RUNTIMESERVICE
#define RUNTIMEFUNCTION
#define RUNTIME_CODE(a)
#define BEGIN_RUNTIME_DATA()
#define END_RUNTIME_DATA()
#define VOLATILE volatile
#define MEMORY_FENCE()
#define EXPORTAPI
#define POST_CODE(_Data)
#define INTERFACE_DECL(x) typedef struct x x
#ifndef uefi_call_wrapper
#define uefi_call_wrapper(func, nargs, ...) (func)(__VA_ARGS__)
#endif

#endif
