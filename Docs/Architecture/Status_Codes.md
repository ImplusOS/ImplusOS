# Status Codes Reference

ImplusOS kernel subsystems use `os_status_t` (typedef for `int64_t`) as a
unified error reporting type. Defined in `Kernel/Common/Status.h`.

---

## Convention

- **Zero** (`OS_STATUS_OK`) means success.
- **Negative** values indicate errors.
- **Positive** values are reserved for future use (currently unused).

```c
os_status_t result = some_kernel_function();
if (os_status_is_error(result)) {
    // handle error
}
```

---

## Status Codes

| Macro | Value | Errno Mapping | Description |
|---|---|---|---|
| `OS_STATUS_OK` | `0` | `0` | Operation succeeded |
| `OS_STATUS_NOT_FOUND` | `-2` | `2` (`ENOENT`) | File or resource not found |
| `OS_STATUS_IO_ERROR` | `-5` | `5` (`EIO`) | I/O error |
| `OS_STATUS_ACCESS_DENIED` | `-13` | `13` (`EACCES`) | Permission denied |
| `OS_STATUS_FAULT` | `-14` | `14` (`EFAULT`) | Bad address / memory fault |
| `OS_STATUS_INVALID_ARG` | `-22` | `22` (`EINVAL`) | Invalid argument |
| `OS_STATUS_LIMIT_REACHED` | `-24` | `24` (`EMFILE`) | Resource limit reached |
| `OS_STATUS_NOT_SUPPORTED` | `-95` | `95` (`EOPNOTSUPP`) | Operation not supported |
| `OS_STATUS_INTERNAL` | `-255` | `255` | Internal error (catch-all) |

---

## Utility Functions

| Function | Signature | Description |
|---|---|---|
| `os_status_is_error` | `int (os_status_t)` | Returns non-zero if status < 0 |
| `os_status_from_i32` | `os_status_t (int32_t)` | Cast from int32 |
| `os_status_to_u64` | `uint64_t (os_status_t)` | Cast to unsigned (for syscall return) |
| `os_status_abs_u64` | `uint64_t (os_status_t)` | Absolute value as unsigned |
| `os_status_to_string` | `const char* (os_status_t)` | Human-readable name |
| `os_status_to_errno` | `int32_t (os_status_t)` | Convert to POSIX-like errno |

---

## Errno Conversion

The `os_status_to_errno()` function maps kernel status codes to POSIX-compatible
errno values for userland consumption. For unknown negative values, the absolute
magnitude is returned directly (clamped to `INT32_MAX`).

### Example Usage (Userland)

```c
// Userland wrapper (simplified)
int fd = syscall_file_open(path, mode);
if (fd < 0) {
    os_errno = os_status_to_errno(fd);
    return -1;
}
```

---

## Adding New Status Codes

1. Add the enum entry in `Kernel/Common/Status.h`
2. Add a case in `os_status_to_string()`
3. Add a case in `os_status_to_errno()` with the appropriate POSIX mapping
4. Update this document
