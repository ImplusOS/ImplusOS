#pragma once
#include <stddef.h>

void* memset(void* ptr, int v, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int memcmp(const void* a, const void* b, size_t n);

size_t strlen(const char* s);
size_t strnlen(const char* s, size_t max_len);
size_t strlcpy(char* dst, const char* src, size_t dst_size);
size_t strlcat(char* dst, const char* src, size_t dst_size);
char* strcpy(char* d, const char* s);
char* strncpy(char* d, const char* s, size_t n);

int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
int strcasecmp(const char* a, const char* b);
int strncasecmp(const char* a, const char* b, size_t n);

char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);
char* strtok(char* str, const char* delim);
char* strtok_r(char* str, const char* delim, char** saveptr);
char* strdup(const char* s);
char* strcat(char* dst, const char* src);
char* strncat(char* dst, const char* src, size_t n);
const char* strerror(int errnum);
void* memchr(const void* s, int c, size_t n);
void* memrchr(const void* s, int c, size_t n);

size_t strspn(const char* s, const char* accept);
size_t strcspn(const char* s, const char* reject);
char* strpbrk(const char* s, const char* accept);

const char* strsignal(int signum);
