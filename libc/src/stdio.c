#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

int vsnprintf(char* str, size_t size, const char* format, va_list ap) {
    if (size == 0) return 0;
    size_t i = 0;
    const char* f = format;
    while (*f && i < size - 1) {
        if (*f == '%') {
            f++;
            int pad_zero = 0;
            int width = 0;
            if (*f == '0') {
                pad_zero = 1;
                f++;
            }
            while (*f >= '0' && *f <= '9') {
                width = width * 10 + (*f - '0');
                f++;
            }

            int is_long = 0;
            while (*f == 'l') {
                is_long++;
                f++;
            }

            if (*f == 's') {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s && i < size - 1) str[i++] = *s++;
            } else if (*f == 'd' || *f == 'i') {
                int64_t d;
                if (is_long >= 2) d = va_arg(ap, int64_t);
                else if (is_long == 1) d = va_arg(ap, long);
                else d = va_arg(ap, int);
                
                char buf[32];
                int j = 0;
                uint64_t u_val;
                int negative = 0;
                if (d < 0) {
                    negative = 1;
                    u_val = (uint64_t)-d;
                } else {
                    u_val = (uint64_t)d;
                }
                if (u_val == 0) buf[j++] = '0';
                while (u_val > 0) {
                    buf[j++] = (char)(u_val % 10 + '0');
                    u_val /= 10;
                }
                int total_len = j + negative;
                if (negative && i < size - 1) str[i++] = '-';
                while (width > total_len && i < size - 1) {
                    str[i++] = pad_zero ? '0' : ' ';
                    width--;
                }
                while (j > 0 && i < size - 1) str[i++] = buf[--j];
            } else if (*f == 'p' || *f == 'x' || *f == 'X') {
                uint64_t x;
                if (*f == 'p' || is_long >= 1) x = va_arg(ap, uint64_t);
                else x = va_arg(ap, uint32_t);

                char buf[32];
                int j = 0;
                if (x == 0) buf[j++] = '0';
                while (x > 0) {
                    int digit = (int)(x % 16);
                    if (*f == 'X')
                        buf[j++] = (char)(digit < 10 ? digit + '0' : digit - 10 + 'A');
                    else
                        buf[j++] = (char)(digit < 10 ? digit + '0' : digit - 10 + 'a');
                    x /= 16;
                }
                while (width > j && i < size - 1) {
                    str[i++] = pad_zero ? '0' : ' ';
                    width--;
                }
                while (j > 0 && i < size - 1) str[i++] = buf[--j];
            } else if (*f == 'u') {
                uint64_t u;
                if (is_long >= 2) u = va_arg(ap, uint64_t);
                else if (is_long == 1) u = va_arg(ap, unsigned long);
                else u = va_arg(ap, uint32_t);

                char buf[32];
                int j = 0;
                if (u == 0) buf[j++] = '0';
                while (u > 0) {
                    buf[j++] = (char)(u % 10 + '0');
                    u /= 10;
                }
                while (width > j && i < size - 1) {
                    str[i++] = pad_zero ? '0' : ' ';
                    width--;
                }
                while (j > 0 && i < size - 1) str[i++] = buf[--j];
            } else if (*f == '%') {
                if (i < size - 1) str[i++] = '%';
            }
            if (!*f) break;
        } else {
            str[i++] = *f;
        }
        f++;
    }
    str[i] = '\0';
    return (int)i;
}

int snprintf(char* str, size_t size, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(str, size, format, ap);
    va_end(ap);
    return res;
}

int sprintf(char* str, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(str, 1024, format, ap);
    va_end(ap);
    return res;
}

int printf(const char* format, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    write(1, buf, (size_t)res);
    return res;
}

int fprintf(FILE* stream, const char* format, ...) {
    char buf[1024];
    int len;
    va_list ap;
    va_start(ap, format);
    len = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    if (len > 0) {
        if (write(stream->fd, buf, (size_t)len) != len) return -1;
    }
    return len;
}

int vfprintf(FILE* stream, const char* format, va_list ap) {
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), format, ap);
    if (len > 0) {
        if (write(stream->fd, buf, (size_t)len) != len) return -1;
    }
    return len;
}
