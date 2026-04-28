#pragma once
#include <stdint.h>

static inline int implus_unix_socket(int type) {
    long r; __asm__ volatile("syscall":"=a"(r):"a"(220ULL),"D"((long)type):"rcx","r11","memory"); return (int)r;
}
static inline int implus_unix_bind(int fd, const char *path) {
    long r; __asm__ volatile("syscall":"=a"(r):"a"(221ULL),"D"((long)fd),"S"((long)path):"rcx","r11","memory"); return (int)r;
}
static inline int implus_unix_listen(int fd, int backlog) {
    long r; __asm__ volatile("syscall":"=a"(r):"a"(222ULL),"D"((long)fd),"S"((long)backlog):"rcx","r11","memory"); return (int)r;
}
static inline int implus_unix_accept(int fd) {
    long r; __asm__ volatile("syscall":"=a"(r):"a"(223ULL),"D"((long)fd):"rcx","r11","memory"); return (int)r;
}
static inline int implus_unix_connect(int fd, const char *path) {
    long r; __asm__ volatile("syscall":"=a"(r):"a"(224ULL),"D"((long)fd),"S"((long)path):"rcx","r11","memory"); return (int)r;
}
static inline long implus_unix_send(int fd, const void *buf, unsigned long len) {
    long r; __asm__ volatile("syscall":"=a"(r):"a"(225ULL),"D"((long)fd),"S"((long)buf),"d"((long)len):"rcx","r11","memory"); return r;
}
static inline long implus_unix_recv(int fd, void *buf, unsigned long len) {
    long r; __asm__ volatile("syscall":"=a"(r):"a"(226ULL),"D"((long)fd),"S"((long)buf),"d"((long)len):"rcx","r11","memory"); return r;
}
static inline long implus_unix_sendmsg(int fd, void *msg) {
    long r; __asm__ volatile("syscall":"=a"(r):"a"(227ULL),"D"((long)fd),"S"((long)msg):"rcx","r11","memory"); return r;
}
static inline long implus_unix_recvmsg(int fd, void *msg) {
    long r; __asm__ volatile("syscall":"=a"(r):"a"(228ULL),"D"((long)fd),"S"((long)msg):"rcx","r11","memory"); return r;
}
static inline int implus_unix_close(int fd) {
    long r; __asm__ volatile("syscall":"=a"(r):"a"(229ULL),"D"((long)fd):"rcx","r11","memory"); return (int)r;
}
