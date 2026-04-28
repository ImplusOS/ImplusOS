#include <../include/stdio.h>
#include <../include/stdarg.h>
#include <../include/stdint.h>
#include <../include/string.h>
#include <../include/errno.h>
#include <../include/fcntl.h>
#include <../include/stdlib.h>
#include <../include/sys/stat.h>
#include <../include/unistd.h>

#ifdef KERNEL
extern void serial_write_string(const char* s);
#define OUTPUT_FUNC(s) serial_write_string(s)
#else
extern void syscall1(uint64_t num, uint64_t arg1);
#define SYSCALL_SERIAL_PUTS 2ULL
#define OUTPUT_FUNC(s) syscall1(SYSCALL_SERIAL_PUTS, (uint64_t)s)
#endif

#ifndef KERNEL
static FILE g_stdin_storage  = { .fd = 0, .owned = 0 };
static FILE g_stdout_storage = { .fd = 1, .owned = 0 };
static FILE g_stderr_storage = { .fd = 2, .owned = 0 };

FILE* stdin  = &g_stdin_storage;
FILE* stdout = &g_stdout_storage;
FILE* stderr = &g_stderr_storage;
#endif

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

            if (*f == 's') {
                const char* s = va_arg(ap, const char*);
                while (s && *s && i < size - 1) str[i++] = *s++;
            } else if (*f == 'd') {
                int d = va_arg(ap, int);
                char buf[12];
                int j = 0;
                int negative = 0;
                if (d < 0) {
                    negative = 1;
                    d = -d;
                }
                if (d == 0) buf[j++] = '0';
                while (d > 0) {
                    buf[j++] = (char)(d % 10 + '0');
                    d /= 10;
                }
                int total_len = j + negative;
                if (negative && i < size - 1) str[i++] = '-';
                while (width > total_len && i < size - 1) {
                    str[i++] = pad_zero ? '0' : ' ';
                    width--;
                }
                while (j > 0 && i < size - 1) str[i++] = buf[--j];
            } else if (*f == 'p' || *f == 'x') {
                uint64_t x = va_arg(ap, uint64_t);
                char buf[17];
                int j = 0;
                if (x == 0) buf[j++] = '0';
                while (x > 0) {
                    int digit = (int)(x % 16);
                    buf[j++] = (char)(digit < 10 ? digit + '0' : digit - 10 + 'a');
                    x /= 16;
                }
                while (width > j && i < size - 1) {
                    str[i++] = pad_zero ? '0' : ' ';
                    width--;
                }
                while (j > 0 && i < size - 1) str[i++] = buf[--j];
            } else if (*f == 'u') {
                uint32_t u = va_arg(ap, uint32_t);
                char buf[12];
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
                str[i++] = '%';
            }
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
    OUTPUT_FUNC(buf);
    return res;
}

int vfprintf(FILE* stream, const char* format, va_list ap)
{
    char buf[1024];
    int len;

    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    len = vsnprintf(buf, sizeof(buf), format, ap);
#ifdef KERNEL
    (void)stream;
    OUTPUT_FUNC(buf);
    return len;
#else
    if (write(stream->fd, buf, (size_t)len) < 0) {
        stream->error = 1;
        return -1;
    }
    return len;
#endif
}

int fprintf(FILE* stream, const char* format, ...)
{
    va_list ap;
    int res;
    va_start(ap, format);
    res = vfprintf(stream, format, ap);
    va_end(ap);
    return res;
}

int putchar(int c) {
    char buf[2] = {(char)c, '\0'};
    OUTPUT_FUNC(buf);
    return c;
}

int puts(const char* s) {
    OUTPUT_FUNC(s);
    OUTPUT_FUNC("\n");
    return 0;
}

#ifndef KERNEL
static int stdio_parse_mode(const char* mode, int* open_flags, int* readable, int* writable)
{
    if (!mode || !open_flags || !readable || !writable) {
        errno = EINVAL;
        return -1;
    }

    *open_flags = 0;
    *readable = 0;
    *writable = 0;

    switch (mode[0]) {
        case 'r':
            *open_flags = O_RDONLY;
            *readable = 1;
            break;
        case 'w':
            *open_flags = O_WRONLY | O_CREAT | O_TRUNC;
            *writable = 1;
            break;
        case 'a':
            *open_flags = O_WRONLY | O_CREAT | O_APPEND;
            *writable = 1;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if (mode[1] == '+') {
        *open_flags &= ~(O_RDONLY | O_WRONLY);
        *open_flags |= O_RDWR;
        *readable = 1;
        *writable = 1;
    }

    return 0;
}

FILE* fdopen(int fd, const char* mode)
{
    FILE* stream;
    int flags = 0;
    int readable = 0;
    int writable = 0;

    if (stdio_parse_mode(mode, &flags, &readable, &writable) < 0) {
        return NULL;
    }

    stream = (FILE*)malloc(sizeof(FILE));
    if (!stream) {
        errno = ENOMEM;
        return NULL;
    }

    memset(stream, 0, sizeof(*stream));
    stream->fd = fd;
    stream->owned = 0;
    stream->last_op = readable ? 1 : 2;
    return stream;
}

FILE* fopen(const char* path, const char* mode)
{
    int flags = 0;
    int readable = 0;
    int writable = 0;
    int fd;
    FILE* stream;

    if (stdio_parse_mode(mode, &flags, &readable, &writable) < 0) {
        return NULL;
    }

    fd = open(path, flags);
    if (fd < 0) {
        return NULL;
    }

    stream = (FILE*)malloc(sizeof(FILE));
    if (!stream) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }

    memset(stream, 0, sizeof(*stream));
    stream->fd = fd;
    stream->owned = 1;
    stream->last_op = readable ? 1 : 2;
    return stream;
}

int fclose(FILE* stream)
{
    int rc = 0;
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    if (stream->owned) {
        rc = close(stream->fd);
    }
    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }
    return rc;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    ssize_t rc;
    size_t total;

    if (!stream || !ptr || size == 0 || nmemb == 0) {
        return 0;
    }

    total = size * nmemb;
    if (stream->has_unget && total > 0) {
        ((unsigned char*)ptr)[0] = stream->unget_char;
        stream->has_unget = 0;
        if (total == 1) {
            return 1;
        }
        rc = read(stream->fd, (unsigned char*)ptr + 1, total - 1);
        if (rc < 0) {
            stream->error = 1;
            return 0;
        }
        if (rc < (ssize_t)(total - 1)) {
            stream->eof = 1;
        }
        return (size_t)(rc + 1) / size;
    }

    rc = read(stream->fd, ptr, total);
    if (rc < 0) {
        stream->error = 1;
        return 0;
    }
    if ((size_t)rc < total) {
        stream->eof = 1;
    }
    return (size_t)rc / size;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    ssize_t rc;
    size_t total;
    if (!stream || !ptr || size == 0 || nmemb == 0) {
        return 0;
    }
    total = size * nmemb;
    rc = write(stream->fd, ptr, total);
    if (rc < 0) {
        stream->error = 1;
        return 0;
    }
    return (size_t)rc / size;
}

int fflush(FILE* stream)
{
    (void)stream;
    return 0;
}

int fseek(FILE* stream, long offset, int whence)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    stream->has_unget = 0;
    return (lseek(stream->fd, offset, whence) < 0) ? -1 : 0;
}

long ftell(FILE* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1L;
    }
    return (long)lseek(stream->fd, 0, SEEK_CUR);
}

void rewind(FILE* stream)
{
    if (!stream) {
        return;
    }
    clearerr(stream);
    (void)fseek(stream, 0, SEEK_SET);
}

int fgetc(FILE* stream)
{
    unsigned char ch;
    if (!stream) {
        errno = EINVAL;
        return EOF;
    }
    if (stream->has_unget) {
        stream->has_unget = 0;
        return (int)stream->unget_char;
    }
    if (read(stream->fd, &ch, 1) != 1) {
        stream->eof = 1;
        return EOF;
    }
    return (int)ch;
}

char* fgets(char* s, int size, FILE* stream)
{
    int i = 0;
    int ch;
    if (!s || size <= 0 || !stream) {
        errno = EINVAL;
        return NULL;
    }
    while (i < size - 1) {
        ch = fgetc(stream);
        if (ch == EOF) {
            break;
        }
        s[i++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }
    if (i == 0) {
        return NULL;
    }
    s[i] = '\0';
    return s;
}

int fputc(int c, FILE* stream)
{
    unsigned char ch = (unsigned char)c;
    if (!stream) {
        errno = EINVAL;
        return EOF;
    }
    if (write(stream->fd, &ch, 1) != 1) {
        stream->error = 1;
        return EOF;
    }
    return c;
}

int fputs(const char* s, FILE* stream)
{
    size_t len;
    if (!s || !stream) {
        errno = EINVAL;
        return EOF;
    }
    len = strlen(s);
    return (write(stream->fd, s, len) < 0) ? EOF : 0;
}

int feof(FILE* stream)
{
    return stream ? stream->eof : 0;
}

int ferror(FILE* stream)
{
    return stream ? stream->error : 0;
}

void clearerr(FILE* stream)
{
    if (!stream) {
        return;
    }
    stream->eof = 0;
    stream->error = 0;
}

int fileno(FILE* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    return stream->fd;
}

int remove(const char* path)
{
    return unlink(path);
}

void perror(const char* s)
{
    if (s && *s) {
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
    } else {
        fprintf(stderr, "%s\n", strerror(errno));
    }
}
#endif
