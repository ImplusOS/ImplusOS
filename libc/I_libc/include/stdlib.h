#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

void* malloc(size_t size);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void free(void* p);

#define malloc malloc
#define free free

long strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char* nptr, char** endptr, int base);
long long strtoll(const char* nptr, char** endptr, int base);
unsigned long long strtoull(const char* nptr, char** endptr, int base);
double strtod(const char* nptr, char** endptr);
float strtof(const char* nptr, char** endptr);
long double strtold(const char* nptr, char** endptr);
double atof(const char* nptr);

void exit(int status);
void abort(void);
int atexit(void (*function)(void));

int atoi(const char* nptr);
long atol(const char* nptr);
int abs(int n);
long labs(long n);
long long llabs(long long n);

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;

div_t div(int num, int denom);
ldiv_t ldiv(long num, long denom);
lldiv_t lldiv(long long num, long long denom);

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*));

#define RAND_MAX 0x7fff

#ifdef IMPLUSOS_FFMPEG_BUILD
int posix_memalign(void **ptr, size_t align, size_t size);
#endif

#ifndef IMPLUSOS_FFMPEG_BUILD
char* getenv(const char* name);
#endif
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *string);
char* realpath(const char* path, char* resolved_path);

int rand(void);
void srand(unsigned int seed);

int system(const char *command);
