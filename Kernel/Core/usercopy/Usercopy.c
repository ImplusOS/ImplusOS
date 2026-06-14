#include "Usercopy.h"

#include "Core/process/ProcessManager.h"
#include <stddef.h>
#include <string.h>

uint64_t copy_from_user(void *kernel_dst, const void *user_src, uint64_t bytes)
{
    if (bytes == 0) {
        return 0;
    }
    if (kernel_dst == 0 || user_src == 0 ||
        !process_user_buffer_is_valid(user_src, bytes)) {
        return bytes;
    }

    memcpy(kernel_dst, user_src, (size_t)bytes);
    return 0;
}

uint64_t copy_to_user(void *user_dst, const void *kernel_src, uint64_t bytes)
{
    if (bytes == 0) {
        return 0;
    }
    if (user_dst == 0 || kernel_src == 0 ||
        !process_user_buffer_is_valid(user_dst, bytes)) {
        return bytes;
    }

    memcpy(user_dst, kernel_src, (size_t)bytes);
    return 0;
}

int copy_user_cstring_s(char *dst, uint64_t dst_size, const char *user_src)
{
    if (dst == 0 || user_src == 0 || dst_size < 2u) {
        return -1;
    }

    for (uint64_t i = 0; i + 1u < dst_size; ++i) {
        char ch = '\0';
        if (copy_from_user(&ch, user_src + i, 1u) != 0u) {
            dst[0] = '\0';
            return -1;
        }
        dst[i] = ch;
        if (ch == '\0') {
            return 0;
        }
    }

    dst[0] = '\0';
    return -1;
}
