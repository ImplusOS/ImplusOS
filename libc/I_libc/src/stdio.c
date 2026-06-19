#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/syscalls.h>
#include <fcntl.h>
#include <math.h>

#ifndef KERNEL

static FILE __stdin = { 0, 0, 0, 1, 0, 0, 0 };
static FILE __stdout = { 1, 0, 0, 1, 0, 0, 0 };
static FILE __stderr = { 2, 0, 0, 1, 0, 0, 0 };

FILE* stdin = &__stdin;
FILE* stdout = &__stdout;
FILE* stderr = &__stderr;

extern int64_t file_read(int32_t fd, void* buffer, uint64_t len);
extern int64_t file_write(int32_t fd, const void* buffer, uint64_t len);
extern int64_t file_seek(int32_t fd, int64_t offset, int32_t whence);
extern int32_t file_close(int32_t fd);
extern int32_t file_open(const char* path, uint64_t flags);
extern int32_t file_creat(const char* path);
extern int32_t file_unlink(const char* path);

extern uint64_t syscall0(uint64_t number);
extern void serial_write_char(char c);

static ssize_t stdio_write_fd(int fd, const void* buffer, size_t len)
{
    ssize_t written = write(fd, buffer, len);
    if (written < 0 && buffer != NULL &&
        (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        const char* bytes = (const char*)buffer;
        for (size_t i = 0; i < len; ++i) {
            serial_write_char(bytes[i]);
        }
        return (ssize_t)len;
    }
    return written;
}

static int parse_mode(const char* mode, uint64_t* out_flags)
{
    if (!mode || !out_flags) return -1;
    *out_flags = 0;
    if (mode[0] == 'r') {
        *out_flags = 0;
        if (mode[1] == '+') *out_flags = 2;
    } else if (mode[0] == 'w') {
        *out_flags = 1;
        if (mode[1] == '+') *out_flags = 2;
    } else if (mode[0] == 'a') {
        *out_flags = 1;
        if (mode[1] == '+') *out_flags = 2;
    } else {
        return -1;
    }
    return 0;
}

FILE* fopen(const char* path, const char* mode)
{
    uint64_t flags;
    FILE* stream;
    int fd;

    if (parse_mode(mode, &flags) < 0) {
        errno = EINVAL;
        return NULL;
    }

    if (mode[0] == 'w') {
        file_unlink(path);
        fd = file_creat(path);
    } else {
        fd = file_open(path, flags);
    }

    if (fd < 0) return NULL;

    stream = (FILE*)malloc(sizeof(FILE));
    if (!stream) {
        file_close(fd);
        errno = ENOMEM;
        return NULL;
    }

    stream->fd = fd;
    stream->eof = 0;
    stream->error = 0;
    stream->owned = 1;
    stream->last_op = 0;
    stream->has_unget = 0;
    stream->unget_char = 0;

    if (mode[0] == 'a') {
        file_seek(fd, 0, 2);
    }

    return stream;
}

FILE* fdopen(int fd, const char* mode)
{
    FILE* stream;
    uint64_t flags;
    if (parse_mode(mode, &flags) < 0) {
        errno = EINVAL;
        return NULL;
    }
    stream = (FILE*)malloc(sizeof(FILE));
    if (!stream) {
        errno = ENOMEM;
        return NULL;
    }
    stream->fd = fd;
    stream->eof = 0;
    stream->error = 0;
    stream->owned = 0;
    stream->last_op = 0;
    stream->has_unget = 0;
    stream->unget_char = 0;
    return stream;
}

int fclose(FILE* stream)
{
    int rc = 0;
    if (!stream) {
        errno = EINVAL;
        return EOF;
    }
    if (stream->owned) {
        rc = file_close(stream->fd);
    }
    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }
    return rc;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    if (!stream || !ptr || size == 0) return 0;
    size_t total = size * nmemb;
    if (total == 0) return 0;
    int64_t n = file_read(stream->fd, ptr, (uint64_t)total);
    if (n < 0) {
        stream->error = 1;
        return 0;
    }
    if ((size_t)n < total) stream->eof = 1;
    return (size_t)n / size;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    if (!stream || !ptr || size == 0) return 0;
    size_t total = size * nmemb;
    if (total == 0) return 0;
    ssize_t n = stdio_write_fd(stream->fd, ptr, total);
    if (n < 0) {
        stream->error = 1;
        return 0;
    }
    return (size_t)n / size;
}

int fflush(FILE* stream)
{
    if (!stream) return EOF;
    (void)stream;
    return 0;
}

int fseek(FILE* stream, long offset, int whence)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    stream->eof = 0;
    stream->has_unget = 0;
    int64_t rc = file_seek(stream->fd, (int64_t)offset, (int32_t)whence);
    if (rc < 0) {
        stream->error = 1;
        return -1;
    }
    return 0;
}

long ftell(FILE* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    int64_t pos = file_seek(stream->fd, 0, 1);
    if (pos < 0) {
        stream->error = 1;
        return -1;
    }
    return (long)pos;
}

void rewind(FILE* stream)
{
    if (!stream) return;
    fseek(stream, 0L, 0);
    stream->error = 0;
}

int fgetc(FILE* stream)
{
    unsigned char c;
    if (!stream) return EOF;
    if (stream->has_unget) {
        stream->has_unget = 0;
        return (unsigned char)stream->unget_char;
    }
    int64_t n = file_read(stream->fd, &c, 1);
    if (n <= 0) {
        if (n == 0) stream->eof = 1;
        else stream->error = 1;
        return EOF;
    }
    return (int)c;
}

int ungetc(int c, FILE* stream)
{
    if (!stream || c == EOF || stream->has_unget) {
        return EOF;
    }

    stream->has_unget = 1;
    stream->unget_char = (unsigned char)c;
    stream->eof = 0;
    return (unsigned char)c;
}

char* fgets(char* s, int size, FILE* stream)
{
    int i = 0;
    int c;

    if (!s || size <= 0 || !stream) return NULL;

    while (i < size - 1) {
        c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

int fputc(int c, FILE* stream)
{
    unsigned char ch = (unsigned char)c;
    if (!stream) return EOF;
    ssize_t n = stdio_write_fd(stream->fd, &ch, 1);
    if (n <= 0) {
        stream->error = 1;
        return EOF;
    }
    return (unsigned char)ch;
}

int fputs(const char* s, FILE* stream)
{
    if (!s || !stream) return EOF;
    size_t len = strlen(s);
    ssize_t n = stdio_write_fd(stream->fd, s, len);
    if (n < 0) {
        stream->error = 1;
        return EOF;
    }
    return 0;
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
    if (stream) {
        stream->eof = 0;
        stream->error = 0;
    }
}

int fileno(FILE* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    return stream->fd;
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int puts(const char* s)
{
    if (fputs(s, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 1;
}

int getchar(void)
{
    return fgetc(stdin);
}

int remove(const char* path)
{
    return file_unlink(path);
}

void perror(const char* s)
{
    if (s && *s) {
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
    } else {
        fprintf(stderr, "%s\n", strerror(errno));
    }
}

int vprintf(const char* format, va_list ap)
{
    char buf[1024];
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    if (res > 0) {
        stdio_write_fd(STDOUT_FILENO, buf, (size_t)res);
    }
    return res;
}

static int skip_isspace(const char** p)
{
    int count = 0;
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') {
        (*p)++;
        count++;
    }
    return count;
}

static int fgetc_or_unget(FILE* stream)
{
    if (stream->has_unget) {
        stream->has_unget = 0;
        return (unsigned char)stream->unget_char;
    }
    return fgetc(stream);
}

static void unget_char(int c, FILE* stream)
{
    if (c != EOF && stream) {
        stream->has_unget = 1;
        stream->unget_char = (unsigned char)c;
    }
}

static int vfscanf_internal(FILE* stream, const char* format, va_list ap)
{
    int match_count = 0;
    int c;
    const char* f = format;

    while (*f) {
        if (*f == ' ' || *f == '\t' || *f == '\n') {
            skip_isspace(&f);
            continue;
        }
        if (*f != '%') {
            c = fgetc_or_unget(stream);
            if (c != (unsigned char)*f) {
                unget_char(c, stream);
                break;
            }
            f++;
            continue;
        }
        f++;
        int suppress = 0;
        int width = 0;
        if (*f == '*') {
            suppress = 1;
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
        if (*f == 'h') {
            is_long = -1;
            f++;
        }

        skip_isspace((const char**)&stream->fd);
        (void)stream;

        if (*f == 'd' || *f == 'i') {
            long val = 0;
            int sign = 1;
            int count = 0;
            c = fgetc_or_unget(stream);
            if (c == '-') { sign = -1; c = fgetc_or_unget(stream); }
            else if (c == '+') { c = fgetc_or_unget(stream); }
            int base = (*f == 'i') ? 0 : 10;
            if (base == 0 && c == '0') {
                int n = fgetc_or_unget(stream);
                if (n == 'x' || n == 'X') { base = 16; c = fgetc_or_unget(stream); }
                else { base = 8; unget_char(n, stream); c = '0'; }
            }
            while ((c >= '0' && c <= '9') ||
                   (base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))) {
                int digit;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else digit = c - 'A' + 10;
                if (width > 0 && count >= width) break;
                val = val * base + digit;
                count++;
                c = fgetc_or_unget(stream);
            }
            unget_char(c, stream);
            if (count > 0) {
                val *= sign;
                if (!suppress) {
                    if (is_long >= 2) *va_arg(ap, long long*) = val;
                    else if (is_long == 1) *va_arg(ap, long*) = val;
                    else *va_arg(ap, int*) = (int)val;
                    match_count++;
                }
            }
        } else if (*f == 'u') {
            unsigned long val = 0;
            int count = 0;
            c = fgetc_or_unget(stream);
            if (c == '+') c = fgetc_or_unget(stream);
            while (c >= '0' && c <= '9') {
                if (width > 0 && count >= width) break;
                val = val * 10 + (unsigned long)(c - '0');
                count++;
                c = fgetc_or_unget(stream);
            }
            unget_char(c, stream);
            if (count > 0 && !suppress) {
                if (is_long >= 2) *va_arg(ap, unsigned long long*) = val;
                else if (is_long == 1) *va_arg(ap, unsigned long*) = val;
                else *va_arg(ap, unsigned int*) = (unsigned int)val;
                match_count++;
            }
        } else if (*f == 'x' || *f == 'X') {
            unsigned long val = 0;
            int count = 0;
            c = fgetc_or_unget(stream);
            if (c == '+') c = fgetc_or_unget(stream);
            if (c == '0') { int n = fgetc_or_unget(stream); if (n != 'x' && n != 'X') unget_char(n, stream); c = fgetc_or_unget(stream); }
            while ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                int digit;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else digit = c - 'A' + 10;
                if (width > 0 && count >= width) break;
                val = val * 16 + (unsigned long)digit;
                count++;
                c = fgetc_or_unget(stream);
            }
            unget_char(c, stream);
            if (count > 0 && !suppress) {
                *va_arg(ap, unsigned int*) = (unsigned int)val;
                match_count++;
            }
        } else if (*f == 's') {
            char* s = suppress ? NULL : va_arg(ap, char*);
            int count = 0;
            c = fgetc_or_unget(stream);
            while (c != EOF && c != ' ' && c != '\t' && c != '\n') {
                if (width > 0 && count >= width) break;
                if (s) s[count] = (char)c;
                count++;
                c = fgetc_or_unget(stream);
            }
            unget_char(c, stream);
            if (s) s[count] = '\0';
            if (count > 0 && !suppress) match_count++;
        } else if (*f == 'c') {
            if (!suppress) {
                char* s = va_arg(ap, char*);
                c = fgetc_or_unget(stream);
                if (c != EOF) {
                    *s = (char)c;
                    match_count++;
                }
            } else {
                fgetc_or_unget(stream);
            }
        } else if (*f == '%') {
            c = fgetc_or_unget(stream);
            if (c != '%') {
                unget_char(c, stream);
                break;
            }
        } else if (*f == 'f' || *f == 'e' || *f == 'g') {
            double val = 0.0;
            int sign = 1;
            int count = 0;
            c = fgetc_or_unget(stream);
            if (c == '-') { sign = -1; c = fgetc_or_unget(stream); }
            else if (c == '+') { c = fgetc_or_unget(stream); }
            while (c >= '0' && c <= '9') {
                if (width > 0 && count >= width) break;
                val = val * 10.0 + (double)(c - '0');
                count++;
                c = fgetc_or_unget(stream);
            }
            if (c == '.') {
                double frac = 0.0;
                double div = 10.0;
                c = fgetc_or_unget(stream);
                while (c >= '0' && c <= '9') {
                    if (width > 0 && count >= width) break;
                    frac += (double)(c - '0') / div;
                    div *= 10.0;
                    count++;
                    c = fgetc_or_unget(stream);
                }
                val += frac;
            }
            if ((c == 'e' || c == 'E') && count > 0) {
                int exp_sign = 1;
                int exp_val = 0;
                c = fgetc_or_unget(stream);
                if (c == '-') { exp_sign = -1; c = fgetc_or_unget(stream); }
                else if (c == '+') { c = fgetc_or_unget(stream); }
                while (c >= '0' && c <= '9') {
                    exp_val = exp_val * 10 + (c - '0');
                    c = fgetc_or_unget(stream);
                }
                val *= pow(10.0, (double)(exp_sign * exp_val));
            }
            unget_char(c, stream);
            val *= sign;
            if (count > 0 && !suppress) {
                if (is_long >= 2) *va_arg(ap, long double*) = (long double)val;
                else if (is_long == 1) *va_arg(ap, double*) = val;
                else *va_arg(ap, float*) = (float)val;
                match_count++;
            }
        } else if (*f == 'n') {
            if (!suppress) {
                long pos = file_seek(stream->fd, 0, 1);
                *va_arg(ap, int*) = (int)pos;
            }
        } else if (*f == '[') {
            f++;
            int invert = 0;
            if (*f == '^') { invert = 1; f++; }
            char accept[256];
            memset(accept, 0, sizeof(accept));
            int first = 1;
            while (*f && *f != ']') {
                if (*f == '-' && !first && *(f+1) && *(f+1) != ']') {
                    for (char ch = *(f-1) + 1; ch < *(f+1); ch++) accept[(unsigned char)ch] = 1;
                    f++;
                } else {
                    accept[(unsigned char)*f] = 1;
                }
                first = 0;
                f++;
            }
            char* s = suppress ? NULL : va_arg(ap, char*);
            int count = 0;
            c = fgetc_or_unget(stream);
            while (c != EOF) {
                int matched = accept[(unsigned char)c] ? 1 : 0;
                if (invert) matched = !matched;
                if (!matched) break;
                if (width > 0 && count >= width) break;
                if (s) s[count] = (char)c;
                count++;
                c = fgetc_or_unget(stream);
            }
            unget_char(c, stream);
            if (s) s[count] = '\0';
            if (count > 0 && !suppress) match_count++;
        } else {
            break;
        }
        f++;
    }
    return match_count;
}

int vfscanf(FILE* stream, const char* format, va_list ap)
{
    return vfscanf_internal(stream, format, ap);
}

int fscanf(FILE* stream, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vfscanf_internal(stream, format, ap);
    va_end(ap);
    return ret;
}

int vscanf(const char* format, va_list ap)
{
    return vfscanf_internal(stdin, format, ap);
}

int scanf(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vfscanf_internal(stdin, format, ap);
    va_end(ap);
    return ret;
}

int vsscanf(const char* str, const char* format, va_list ap)
{
    if (!str) return 0;
    FILE f;
    f.fd = -1;
    f.eof = 0;
    f.error = 0;
    f.owned = 0;
    f.last_op = 0;
    f.has_unget = 0;
    f.unget_char = 0;
    size_t len = strlen(str);
    size_t pos = 0;

    int match_count = 0;
    const char* fp = format;

    while (*fp) {
        if (*fp == ' ' || *fp == '\t' || *fp == '\n') {
            while (*fp == ' ' || *fp == '\t' || *fp == '\n') fp++;
            while (pos < len && (str[pos] == ' ' || str[pos] == '\t' || str[pos] == '\n')) pos++;
            continue;
        }
        if (*fp != '%') {
            if (pos >= len || str[pos] != *fp) break;
            pos++;
            fp++;
            continue;
        }
        fp++;
        int suppress = 0;
        int width = 0;
        if (*fp == '*') { suppress = 1; fp++; }
        while (*fp >= '0' && *fp <= '9') {
            width = width * 10 + (*fp - '0');
            fp++;
        }
        int is_long = 0;
        while (*fp == 'l') { is_long++; fp++; }
        if (*fp == 'h') { is_long = -1; fp++; }

        while (pos < len && (str[pos] == ' ' || str[pos] == '\t' || str[pos] == '\n')) pos++;

        if (*fp == 'd' || *fp == 'i') {
            long val = 0;
            int sign = 1;
            int count = 0;
            if (pos < len && str[pos] == '-') { sign = -1; pos++; }
            else if (pos < len && str[pos] == '+') { pos++; }
            int base = (*fp == 'i') ? 0 : 10;
            if (base == 0 && pos < len && str[pos] == '0') {
                pos++;
                if (pos < len && (str[pos] == 'x' || str[pos] == 'X')) { base = 16; pos++; }
                else { base = 8; }
            }
            while (pos < len) {
                char c = str[pos];
                int digit;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                else break;
                if (width > 0 && count >= width) break;
                val = val * base + digit;
                count++;
                pos++;
            }
            if (count > 0) {
                val *= sign;
                if (!suppress) {
                    if (is_long >= 2) *va_arg(ap, long long*) = val;
                    else if (is_long == 1) *va_arg(ap, long*) = val;
                    else *va_arg(ap, int*) = (int)val;
                    match_count++;
                }
            }
        } else if (*fp == 'u') {
            unsigned long val = 0;
            int count = 0;
            if (pos < len && str[pos] == '+') pos++;
            while (pos < len && str[pos] >= '0' && str[pos] <= '9') {
                if (width > 0 && count >= width) break;
                val = val * 10 + (unsigned long)(str[pos] - '0');
                count++;
                pos++;
            }
            if (count > 0 && !suppress) {
                if (is_long >= 2) *va_arg(ap, unsigned long long*) = val;
                else if (is_long == 1) *va_arg(ap, unsigned long*) = val;
                else *va_arg(ap, unsigned int*) = (unsigned int)val;
                match_count++;
            }
        } else if (*fp == 'x' || *fp == 'X') {
            unsigned long val = 0;
            int count = 0;
            if (pos < len && str[pos] == '0') { pos++; if (pos < len && (str[pos] == 'x' || str[pos] == 'X')) pos++; }
            while (pos < len) {
                char c = str[pos];
                int digit;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                else break;
                if (width > 0 && count >= width) break;
                val = val * 16 + (unsigned long)digit;
                count++;
                pos++;
            }
            if (count > 0 && !suppress) {
                *va_arg(ap, unsigned int*) = (unsigned int)val;
                match_count++;
            }
        } else if (*fp == 's') {
            char* s = suppress ? NULL : va_arg(ap, char*);
            int count = 0;
            while (pos < len && str[pos] != ' ' && str[pos] != '\t' && str[pos] != '\n') {
                if (width > 0 && count >= width) break;
                if (s) s[count] = str[pos];
                count++;
                pos++;
            }
            if (s) s[count] = '\0';
            if (count > 0 && !suppress) match_count++;
        } else if (*fp == 'c') {
            if (!suppress) {
                char* s = va_arg(ap, char*);
                if (pos < len) {
                    *s = str[pos++];
                    match_count++;
                }
            } else {
                if (pos < len) pos++;
            }
        } else if (*fp == '%') {
            if (pos >= len || str[pos] != '%') break;
            pos++;
        } else if (*fp == 'f' || *fp == 'e' || *fp == 'g') {
            double val = 0.0;
            int sign = 1;
            int count = 0;
            if (pos < len && str[pos] == '-') { sign = -1; pos++; }
            else if (pos < len && str[pos] == '+') { pos++; }
            while (pos < len && str[pos] >= '0' && str[pos] <= '9') {
                val = val * 10.0 + (double)(str[pos] - '0');
                count++;
                pos++;
            }
            if (pos < len && str[pos] == '.') {
                double frac = 0.0;
                double div = 10.0;
                pos++;
                while (pos < len && str[pos] >= '0' && str[pos] <= '9') {
                    frac += (double)(str[pos] - '0') / div;
                    div *= 10.0;
                    count++;
                    pos++;
                }
                val += frac;
            }
            if (pos < len && (str[pos] == 'e' || str[pos] == 'E') && count > 0) {
                int exp_sign = 1;
                int exp_val = 0;
                pos++;
                if (pos < len && str[pos] == '-') { exp_sign = -1; pos++; }
                else if (pos < len && str[pos] == '+') { pos++; }
                while (pos < len && str[pos] >= '0' && str[pos] <= '9') {
                    exp_val = exp_val * 10 + (str[pos] - '0');
                    pos++;
                }
                double multiplier = 1.0;
                int e = exp_val;
                if (exp_sign == -1) { while (e-- > 0) multiplier /= 10.0; }
                else { while (e-- > 0) multiplier *= 10.0; }
                val *= multiplier;
            }
            val *= sign;
            if (count > 0 && !suppress) {
                if (is_long >= 2) *va_arg(ap, long double*) = (long double)val;
                else if (is_long == 1) *va_arg(ap, double*) = val;
                else *va_arg(ap, float*) = (float)val;
                match_count++;
            }
        } else if (*fp == 'n') {
            if (!suppress) {
                *va_arg(ap, int*) = (int)pos;
            }
        } else if (*fp == '[') {
            fp++;
            int invert = 0;
            if (*fp == '^') { invert = 1; fp++; }
            char accept[256];
            memset(accept, 0, sizeof(accept));
            while (*fp && *fp != ']') {
                accept[(unsigned char)*fp] = 1;
                fp++;
            }
            char* s = suppress ? NULL : va_arg(ap, char*);
            int count = 0;
            while (pos < len) {
                int matched = accept[(unsigned char)str[pos]] ? 1 : 0;
                if (invert) matched = !matched;
                if (!matched) break;
                if (width > 0 && count >= width) break;
                if (s) s[count] = str[pos];
                count++;
                pos++;
            }
            if (s) s[count] = '\0';
            if (count > 0 && !suppress) match_count++;
        } else {
            break;
        }
        fp++;
    }
    return match_count;
}

int sscanf(const char* str, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vsscanf(str, format, ap);
    va_end(ap);
    return ret;
}

ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream)
{
    if (!lineptr || !n || !stream) {
        errno = EINVAL;
        return -1;
    }
    if (*lineptr == NULL) {
        *n = 128;
        *lineptr = (char*)malloc(*n);
        if (!*lineptr) {
            errno = ENOMEM;
            return -1;
        }
    }
    size_t pos = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 2 > *n) {
            size_t new_n = *n * 2;
            char *new_ptr = (char*)realloc(*lineptr, new_n);
            if (!new_ptr) return -1;
            *lineptr = new_ptr;
            *n = new_n;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == delim) break;
    }
    if (pos == 0 && c == EOF) return -1;
    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
    return getdelim(lineptr, n, '\n', stream);
}

FILE *tmpfile(void)
{
    static unsigned long counter = 0;
    char path[64];
    for (int attempt = 0; attempt < 100; attempt++) {
        snprintf(path, sizeof(path), "/tmp/tmp_%lu_%d", counter++, attempt);
        FILE *f = fopen(path, "w+");
        if (f) return f;
    }
    return NULL;
}

char *tmpnam(char *s)
{
    static unsigned long counter = 0;
    static char buf[64];
    char *result = s ? s : buf;
    snprintf(result, 64, "/tmp/tmp_%lu", counter++);
    return result;
}

#else /* KERNEL mode */
FILE* stdin = (FILE*)0;
FILE* stdout = (FILE*)0;
FILE* stderr = (FILE*)0;
#endif /* !KERNEL */

int vsnprintf(char* str, size_t size, const char* format, va_list ap) {
    if (size == 0) return 0;
    size_t i = 0;
    const char* f = format;
    while (*f && i < size - 1) {
        if (*f == '%') {
            f++;
            int pad_zero = 0;
            int left_align = 0;
            int force_sign = 0;
            int space_sign = 0;
            int alt_form = 0;
            int width = 0;
            int precision = -1;
            int have_precision = 0;

            while (*f == '-') { left_align = 1; f++; }
            while (*f == '+') { force_sign = 1; f++; }
            while (*f == ' ') { space_sign = 1; f++; }
            while (*f == '#') { alt_form = 1; f++; }
            if (*f == '0') {
                if (!left_align) pad_zero = 1;
                f++;
            }
            while (*f >= '0' && *f <= '9') {
                width = width * 10 + (*f - '0');
                f++;
            }
            if (*f == '.') {
                have_precision = 1;
                precision = 0;
                f++;
                while (*f >= '0' && *f <= '9') {
                    precision = precision * 10 + (*f - '0');
                    f++;
                }
            }

            int is_long = 0;
            int is_short = 0;
            int is_long_double = 0;
            while (*f == 'l') { is_long++; f++; }
            if (*f == 'h') { is_short = 1; f++; }
            if (*f == 'L') { is_long_double = 1; f++; }

            if (*f == 's') {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                size_t slen = strlen(s);
                if (have_precision && (int)slen > precision) slen = (size_t)precision;
                size_t pad = (width > (int)slen) ? (size_t)(width - (int)slen) : 0;
                if (!left_align) {
                    for (size_t p = 0; p < pad && i < size - 1; p++) str[i++] = ' ';
                }
                for (size_t j = 0; j < slen && i < size - 1; j++) str[i++] = s[j];
                if (left_align) {
                    for (size_t p = 0; p < pad && i < size - 1; p++) str[i++] = ' ';
                }
            } else if (*f == 'c') {
                char c = (char)va_arg(ap, int);
                if (i < size - 1) str[i++] = c;
            } else if (*f == 'd' || *f == 'i') {
                int64_t d;
                if (is_long >= 2) d = va_arg(ap, int64_t);
                else if (is_long == 1) d = va_arg(ap, long);
                else if (is_short) d = (short)va_arg(ap, int);
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
                int printed = 0;
                if (u_val == 0 && !(have_precision && precision == 0)) buf[j++] = '0';
                while (u_val > 0) {
                    buf[j++] = (char)(u_val % 10 + '0');
                    u_val /= 10;
                }
                int sign_chars = negative ? 1 : (force_sign ? 1 : (space_sign ? 1 : 0));
                int precision_pad = 0;
                if (have_precision && precision > j) precision_pad = precision - j;
                int total_len = j + precision_pad + sign_chars;
                char pad_char = pad_zero ? '0' : ' ';
                if (left_align) pad_char = ' ';
                int pad_count = (width > total_len) ? width - total_len : 0;

                if (!left_align && pad_char == ' ') {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = ' ';
                }
                if (negative) { if (i < size - 1) str[i++] = '-'; }
                else if (force_sign) { if (i < size - 1) str[i++] = '+'; }
                else if (space_sign) { if (i < size - 1) str[i++] = ' '; }
                if (!left_align && pad_char == '0') {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = '0';
                }
                for (int p = 0; p < precision_pad && i < size - 1; p++) str[i++] = '0';
                while (j > 0 && i < size - 1) { printed = 1; str[i++] = buf[--j]; }
                if (left_align) {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = ' ';
                }
                (void)printed;
            } else if (*f == 'p') {
                uint64_t x = va_arg(ap, uint64_t);
                char buf[32];
                int j = 0;
                if (x == 0) buf[j++] = '0';
                while (x > 0) {
                    int digit = (int)(x % 16);
                    buf[j++] = (char)(digit < 10 ? digit + '0' : digit - 10 + 'a');
                    x /= 16;
                }
                if (i < size - 1) str[i++] = '0';
                if (i < size - 1) str[i++] = 'x';
                while (j > 0 && i < size - 1) str[i++] = buf[--j];
            } else if (*f == 'x' || *f == 'X') {
                uint64_t x;
                if (is_long >= 1) x = va_arg(ap, uint64_t);
                else x = va_arg(ap, uint32_t);

                char buf[32];
                int j = 0;
                int printed = 0;
                if (x == 0 && !(have_precision && precision == 0)) buf[j++] = '0';
                while (x > 0) {
                    int digit = (int)(x % 16);
                    if (*f == 'X')
                        buf[j++] = (char)(digit < 10 ? digit + '0' : digit - 10 + 'A');
                    else
                        buf[j++] = (char)(digit < 10 ? digit + '0' : digit - 10 + 'a');
                    x /= 16;
                }
                int precision_pad = 0;
                if (have_precision && precision > j) precision_pad = precision - j;
                int total_len = j + precision_pad + (alt_form && x != 0 ? 2 : 0);
                char pad_char = pad_zero ? '0' : ' ';
                if (left_align) pad_char = ' ';
                int pad_count = (width > total_len) ? width - total_len : 0;

                if (!left_align && pad_char == ' ') {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = ' ';
                }
                if (alt_form) {
                    if (i < size - 1) str[i++] = '0';
                    if (i < size - 1) str[i++] = (*f == 'X') ? 'X' : 'x';
                }
                if (!left_align && pad_char == '0') {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = '0';
                }
                for (int p = 0; p < precision_pad && i < size - 1; p++) str[i++] = '0';
                while (j > 0 && i < size - 1) { printed = 1; str[i++] = buf[--j]; }
                if (left_align) {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = ' ';
                }
                (void)printed;
            } else if (*f == 'u') {
                uint64_t u;
                if (is_long >= 2) u = va_arg(ap, uint64_t);
                else if (is_long == 1) u = va_arg(ap, unsigned long);
                else u = va_arg(ap, uint32_t);

                char buf[32];
                int j = 0;
                if (u == 0 && !(have_precision && precision == 0)) buf[j++] = '0';
                while (u > 0) {
                    buf[j++] = (char)(u % 10 + '0');
                    u /= 10;
                }
                int precision_pad = 0;
                if (have_precision && precision > j) precision_pad = precision - j;
                int total_len = j + precision_pad;
                char pad_char = pad_zero ? '0' : ' ';
                if (left_align) pad_char = ' ';
                int pad_count = (width > total_len) ? width - total_len : 0;

                if (!left_align && pad_char == ' ') {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = ' ';
                }
                if (!left_align && pad_char == '0') {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = '0';
                }
                for (int p = 0; p < precision_pad && i < size - 1; p++) str[i++] = '0';
                while (j > 0 && i < size - 1) str[i++] = buf[--j];
                if (left_align) {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = ' ';
                }
            } else if (*f == 'o') {
                uint64_t u;
                if (is_long >= 1) u = va_arg(ap, uint64_t);
                else u = va_arg(ap, uint32_t);

                char buf[32];
                int j = 0;
                if (u == 0 && !(have_precision && precision == 0)) buf[j++] = '0';
                while (u > 0) {
                    buf[j++] = (char)((u % 8) + '0');
                    u /= 8;
                }
                if (alt_form && buf[j-1] != '0') buf[j++] = '0';
                int precision_pad = 0;
                if (have_precision && precision > j) precision_pad = precision - j;
                int total_len = j + precision_pad;
                int pad_count = (width > total_len) ? width - total_len : 0;
                char pad_char = pad_zero && !left_align ? '0' : ' ';
                if (!left_align && pad_char == ' ') {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = ' ';
                }
                if (!left_align && pad_char == '0') {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = '0';
                }
                for (int p = 0; p < precision_pad && i < size - 1; p++) str[i++] = '0';
                while (j > 0 && i < size - 1) str[i++] = buf[--j];
                if (left_align) {
                    for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = ' ';
                }
            } else if (*f == 'n') {
                int *out = va_arg(ap, int*);
                if (out) *out = (int)i;
            } else if (*f == '%') {
                if (i < size - 1) str[i++] = '%';
            } else if (*f == 'f' || *f == 'F') {
                double val;
                if (is_long_double) val = (double)va_arg(ap, long double);
                else val = va_arg(ap, double);

                if (__builtin_isnan(val)) {
                    const char *nan_str = "nan";
                    for (int k = 0; nan_str[k] && i < size - 1; k++) str[i++] = nan_str[k];
                } else if (__builtin_isinf(val)) {
                    if (val < 0) { if (i < size - 1) str[i++] = '-'; }
                    else if (force_sign) { if (i < size - 1) str[i++] = '+'; }
                    const char *inf_str = "inf";
                    for (int k = 0; inf_str[k] && i < size - 1; k++) str[i++] = inf_str[k];
                } else {
                    int prec = (have_precision && precision >= 0) ? precision : 6;
                    if (prec < 0) prec = 6;
                    char fbuf[256];
                    int fi = 0;
                    int neg = 0;
                    if (val < 0) { neg = 1; val = -val; }
                    double int_part;
                    double frac_part = modf(val, &int_part);
                    uint64_t whole = (uint64_t)int_part;
                    char tmp[64];
                    int tj = 0;
                    if (whole == 0) tmp[tj++] = '0';
                    while (whole > 0) {
                        tmp[tj++] = (char)(whole % 10 + '0');
                        whole /= 10;
                    }
                    if (neg) fbuf[fi++] = '-';
                    else if (force_sign) fbuf[fi++] = '+';
                    else if (space_sign) fbuf[fi++] = ' ';
                    while (tj > 0) fbuf[fi++] = tmp[--tj];
                    if (prec > 0 || alt_form) fbuf[fi++] = '.';
                    for (int k = 0; k < prec; k++) {
                        frac_part *= 10.0;
                        int digit = (int)frac_part;
                        fbuf[fi++] = (char)(digit + '0');
                        frac_part -= (double)digit;
                    }
                    fbuf[fi] = '\0';
                    int total_len = fi;
                    int pad_count = (width > total_len) ? width - total_len : 0;
                    char pad_char = pad_zero ? '0' : ' ';
                    if (!left_align) {
                        for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = pad_char;
                    }
                    for (int k = 0; k < fi && i < size - 1; k++) str[i++] = fbuf[k];
                    if (left_align) {
                        for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = ' ';
                    }
                }
            } else if (*f == 'e' || *f == 'E') {
                double val;
                if (is_long_double) val = (double)va_arg(ap, long double);
                else val = va_arg(ap, double);

                if (__builtin_isnan(val) || __builtin_isinf(val)) {
                    if (*f == 'E') {
                        const char *s = __builtin_isnan(val) ? "NAN" : (val < 0 ? "-INF" : "INF");
                        while (*s && i < size - 1) str[i++] = *s++;
                    } else {
                        const char *s = __builtin_isnan(val) ? "nan" : (val < 0 ? "-inf" : "inf");
                        while (*s && i < size - 1) str[i++] = *s++;
                    }
                } else {
                    int prec = (have_precision && precision >= 0) ? precision : 6;
                    int exp = 0;
                    int neg = 0;
                    if (val < 0) { neg = 1; val = -val; }
                    if (val != 0.0) {
                        while (val >= 10.0) { val /= 10.0; exp++; }
                        while (val < 1.0) { val *= 10.0; exp--; }
                    }
                    char fbuf[64];
                    int fi = 0;
                    if (neg) fbuf[fi++] = '-';
                    else if (force_sign) fbuf[fi++] = '+';

                    uint64_t whole = (uint64_t)val;
                    fbuf[fi++] = (char)(whole + '0');
                    double frac = val - (double)whole;
                    if (prec > 0 || alt_form) fbuf[fi++] = '.';
                    for (int k = 0; k < prec; k++) {
                        frac *= 10.0;
                        int digit = (int)frac;
                        fbuf[fi++] = (char)(digit + '0');
                        frac -= (double)digit;
                    }
                    char exp_char = (*f == 'E') ? 'E' : 'e';
                    fbuf[fi++] = exp_char;
                    if (exp < 0) { fbuf[fi++] = '-'; exp = -exp; }
                    else { fbuf[fi++] = '+'; }
                    if (exp < 10) fbuf[fi++] = '0';
                    {
                        char ebuf[8];
                        int ej = 0;
                        if (exp == 0) ebuf[ej++] = '0';
                        while (exp > 0) { ebuf[ej++] = (char)(exp % 10 + '0'); exp /= 10; }
                        while (ej > 0) fbuf[fi++] = ebuf[--ej];
                    }
                    int total_len = fi;
                    int pad_count = (width > total_len) ? width - total_len : 0;
                    char pad_char = pad_zero ? '0' : ' ';
                    if (!left_align) {
                        for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = pad_char;
                    }
                    for (int k = 0; k < fi && i < size - 1; k++) str[i++] = fbuf[k];
                    if (left_align) {
                        for (int p = 0; p < pad_count && i < size - 1; p++) str[i++] = ' ';
                    }
                }
            } else if (*f == 'g' || *f == 'G') {
                double val;
                if (is_long_double) val = (double)va_arg(ap, long double);
                else val = va_arg(ap, double);

                int prec = (have_precision && precision >= 0) ? precision : 6;
                if (prec == 0) prec = 1;

                double abs_val = val < 0 ? -val : val;
                if (abs_val >= 1e4 || abs_val < 1e-4) {
                    double pv = abs_val;
                    int exp = 0;
                    if (pv != 0) { while (pv >= 10.0) { pv /= 10.0; exp++; } while (pv < 1.0) { pv *= 10.0; exp--; } }
                    char buf[128];
                    char *fp = buf;
                    if (val < 0) *fp++ = '-';
                    else if (force_sign) *fp++ = '+';
                    uint64_t w = (uint64_t)pv;
                    fp += snprintf(fp, buf + sizeof(buf) - fp, "%llu", (unsigned long long)w);
                    double fr = pv - (double)w;
                    if (prec > 1 || alt_form) {
                        *fp++ = '.';
                        for (int k = 0; k < prec - 1; k++) { fr *= 10.0; int d = (int)fr; *fp++ = (char)(d + '0'); fr -= (double)d; }
                        int last = (int)(fr * 10.0);
                        if (last >= 5) { fp[-1]++; }
                    }
                    char ec = (*f == 'G') ? 'E' : 'e';
                    *fp++ = ec;
                    if (exp < 0) *fp++ = '-';
                    else *fp++ = '+';
                    fp += snprintf(fp, buf + sizeof(buf) - fp, "%02d", exp < 0 ? -exp : exp);
                    *fp = '\0';
                    int total = (int)(fp - buf);
                    int pad = (width > total) ? width - total : 0;
                    if (!left_align) for (int p = 0; p < pad; p++) str[i++] = ' ';
                    for (int k = 0; k < total; k++) str[i++] = buf[k];
                    if (left_align) for (int p = 0; p < pad; p++) str[i++] = ' ';
                } else {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%.*f", prec - 1, val);
                    char *dot = strchr(buf, '.');
                    if (dot) {
                        char *end = dot + strlen(dot) - 1;
                        while (end > dot && *end == '0') { *end = '\0'; end--; }
                        if (end == dot && !alt_form) *end = '\0';
                    }
                    for (int k = 0; buf[k] && i < size - 1; k++) str[i++] = buf[k];
                }
            } else {
                if (i < size - 1) str[i++] = *f;
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
#ifndef KERNEL
    if (res > 0) {
        stdio_write_fd(STDOUT_FILENO, buf, (size_t)res);
    }
#endif
    return res;
}

int fprintf(FILE* stream, const char* format, ...) {
    char buf[1024];
    int len;
    va_list ap;
    va_start(ap, format);
    len = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
#ifdef KERNEL
    (void)stream;
#else
    if (len > 0) {
        if (stdio_write_fd(stream->fd, buf, (size_t)len) != (ssize_t)len) return -1;
    }
#endif
    return len;
}

int vfprintf(FILE* stream, const char* format, va_list ap) {
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), format, ap);
#ifdef KERNEL
    (void)stream;
#else
    if (len > 0) {
        if (stdio_write_fd(stream->fd, buf, (size_t)len) != (ssize_t)len) return -1;
    }
#endif
    return len;
}
