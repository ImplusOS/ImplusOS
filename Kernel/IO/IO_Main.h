#pragma once
#include <stdbool.h>
#include <stdint.h>
 
static inline void outsw(uint16_t port, const void *addr, int count) {
    __asm__ volatile(
        "rep outsw"
        : "+S"(addr), "+c"(count)
        : "d"(port)
        : "memory"
    );
}
 
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0,%1" :: "a"(val), "Nd"(port));
}
 
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0,%1" :: "a"(val), "Nd"(port));
}
 
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1,%0" : "=a"(val) : "Nd"(port));
    return val;
}
 
static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1,%0" : "=a"(val) : "Nd"(port));
    return val;
}
 
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
 
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
 
typedef enum {
    IO_PROTOCOL_TYPE_NONE,
    IO_PROTOCOL_TYPE_ATA,
    IO_PROTOCOL_TYPE_USB_MASS_STORAGE
} io_protocol_type_t;

void disk_io_init(uint64_t partition_lba, uint32_t boot_drive_type);

bool disk_read(uint32_t lba, uint8_t *buffer, uint32_t sectors);
bool disk_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors);
bool disk_io_is_working(void);
io_protocol_type_t disk_io_get_protocol(void);
