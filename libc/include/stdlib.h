#pragma once
#include <stddef.h>
#include <stdint.h>

void* malloc(size_t size);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void free(void* p);

#define malloc malloc
#define free free

long strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char* nptr, char** endptr, int base);
void exit(int status);
void abort(void);

int atoi(const char* nptr);
long atol(const char* nptr);
int abs(int n);
long labs(long n);

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*));

char* getenv(const char* name);
int rand(void);
void srand(unsigned int seed);
