#pragma once
#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>

typedef struct FILE FILE;

struct FILE {
    int fd;
    int eof;
    int error;
    int owned;
    int last_op;
    int has_unget;
    unsigned char unget_char;
};

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IONBF 0
#define _IOLBF 1
#define _IOFBF 2

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

int printf(const char* format, ...);
int fprintf(FILE* stream, const char* format, ...);
int vfprintf(FILE* stream, const char* format, va_list ap);
int sprintf(char* str, const char* format, ...);
int snprintf(char* str, size_t size, const char* format, ...);
int vsnprintf(char* str, size_t size, const char* format, va_list ap);

int putchar(int c);
int puts(const char* s);
int getchar(void);

FILE* fopen(const char* path, const char* mode);
FILE* fdopen(int fd, const char* mode);
int fclose(FILE* stream);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int fflush(FILE* stream);
void setbuf(FILE* stream, char* buf);
int setvbuf(FILE* stream, char* buf, int mode, size_t size);
int fseek(FILE* stream, long offset, int whence);
long ftell(FILE* stream);
void rewind(FILE* stream);
int fgetc(FILE* stream);
int ungetc(int c, FILE* stream);
char* fgets(char* s, int size, FILE* stream);
int fputc(int c, FILE* stream);
int fputs(const char* s, FILE* stream);
int feof(FILE* stream);
int ferror(FILE* stream);
void clearerr(FILE* stream);
int fileno(FILE* stream);
int remove(const char* path);
int rename(const char* old_path, const char* new_path);
void perror(const char* s);

int vprintf(const char* format, va_list ap);
int fscanf(FILE* stream, const char* format, ...);
int scanf(const char* format, ...);
int sscanf(const char* str, const char* format, ...);
int vfscanf(FILE* stream, const char* format, va_list ap);
int vscanf(const char* format, va_list ap);
int vsscanf(const char* str, const char* format, va_list ap);

ssize_t getline(char **lineptr, size_t *n, FILE *stream);
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream);

FILE *tmpfile(void);
char *tmpnam(char *s);
