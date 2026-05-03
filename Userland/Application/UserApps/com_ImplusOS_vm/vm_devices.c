 

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/syscalls.h>
#include "../../../Syscalls.h"
#include "../../../API/Serial.h"

 
#define SERIAL_BUF_SIZE 256
static char serial_buf[SERIAL_BUF_SIZE];
static uint32_t serial_buf_pos = 0;

static void guest_serial_putchar(char c)
{
    if (c == '\n' || serial_buf_pos >= SERIAL_BUF_SIZE - 1) {
        serial_buf[serial_buf_pos] = '\0';
        printf("[OVMF] %s\n", serial_buf);
        serial_buf_pos = 0;
    } else if (c >= ' ' || c == '\t' || c == '\r') {
        if (c != '\r') {
            serial_buf[serial_buf_pos++] = c;
        }
    }
}

 
typedef struct kvm_run {
    uint8_t  request_interrupt_window;
    uint8_t  immediate_exit;
    uint8_t  _pad_in[6];

    uint32_t exit_reason;
    uint8_t  ready_for_interrupt_injection;
    uint8_t  _pad_out[3];

    union {
        struct {
            uint8_t  direction;   
            uint8_t  size;        
            uint16_t port;
            uint32_t count;
            uint64_t data_offset;
        } io;
        struct {
            uint64_t phys_addr;
            uint8_t  data[8];
            uint32_t len;
            uint8_t  is_write;
            uint8_t  _pad[3];
        } mmio;
        struct {
            uint32_t suberror;
            uint32_t ndata;
            uint64_t data[16];
        } internal;
        uint8_t _pad_exit[256];
    };
    uint8_t io_data[64];
} kvm_run_t;

 
#define COM1_BASE 0x3F8

static uint8_t serial_ier = 0;     
static uint8_t serial_lcr = 0;     
static uint8_t serial_mcr = 0;     
static uint8_t serial_dll = 0;     
static uint8_t serial_dlh = 0;     
static uint8_t serial_fcr = 0;     
static uint8_t serial_scr = 0;     

static void serial_handle_out(uint16_t port, uint8_t val)
{
    uint16_t off = (uint16_t)(port - COM1_BASE);

    if (serial_lcr & 0x80) {
         
        if (off == 0) { serial_dll = val; return; }
        if (off == 1) { serial_dlh = val; return; }
    }

    switch (off) {
    case 0:  
         
        guest_serial_putchar((char)val);
        break;
    case 1:  
        serial_ier = val;
        break;
    case 2:  
        serial_fcr = val;
        break;
    case 3:  
        serial_lcr = val;
        break;
    case 4:  
        serial_mcr = val;
        break;
    case 7:  
        serial_scr = val;
        break;
    default:
        break;
    }
}

static uint8_t serial_handle_in(uint16_t port)
{
    uint16_t off = (uint16_t)(port - COM1_BASE);

    if (serial_lcr & 0x80) {
         
        if (off == 0) return serial_dll;
        if (off == 1) return serial_dlh;
    }

    switch (off) {
    case 0:  
        return 0;
    case 1:  
        return serial_ier;
    case 2:  
        return 0x01;  
    case 3:  
        return serial_lcr;
    case 4:  
        return serial_mcr;
    case 5:  
        return 0x60;  
    case 6:  
        return 0xB0;  
    case 7:  
        return serial_scr;
    default:
        return 0;
    }
}

 
static uint8_t rtc_index = 0;
static uint8_t rtc_regs[128];

static void rtc_init(void)
{
    memset(rtc_regs, 0, sizeof(rtc_regs));
     
    rtc_regs[0x00] = 0x00;  
    rtc_regs[0x02] = 0x00;  
    rtc_regs[0x04] = 0x12;  
    rtc_regs[0x06] = 0x01;  
    rtc_regs[0x07] = 0x01;  
    rtc_regs[0x08] = 0x01;  
    rtc_regs[0x09] = 0x25;  
    rtc_regs[0x0A] = 0x26;  
    rtc_regs[0x0B] = 0x02;  
    rtc_regs[0x0C] = 0x00;  
    rtc_regs[0x0D] = 0x80;  
    rtc_regs[0x32] = 0x20;  
}

static void rtc_handle_out(uint16_t port, uint8_t val)
{
    if (port == 0x70) {
        rtc_index = val & 0x7F;
    } else if (port == 0x71) {
        if (rtc_index < 128) {
            rtc_regs[rtc_index] = val;
        }
    }
}

static uint8_t rtc_handle_in(uint16_t port)
{
    if (port == 0x71 && rtc_index < 128) {
        return rtc_regs[rtc_index];
    }
    return 0;
}

 
static uint32_t pci_config_addr = 0;

static void pci_handle_out(uint16_t port, uint32_t val, uint8_t size)
{
    if (port == 0xCF8 && size == 4) {
        pci_config_addr = val;
    }
     
}

static uint32_t pci_handle_in(uint16_t port, uint8_t size)
{
    if (port == 0xCF8 && size == 4) {
        return pci_config_addr;
    }
     
    if (port >= 0xCFC && port <= 0xCFF) {
        return 0xFFFFFFFF;
    }
    return 0xFFFFFFFF;
}

 
static uint8_t last_post_code = 0;

 
void vm_handle_io(kvm_run_t *run)
{
    uint16_t port = run->io.port;
    uint8_t  size = run->io.size;
    uint8_t  is_in = run->io.direction;

    if (is_in) {
         
        uint32_t val = 0;

        if (port >= COM1_BASE && port <= COM1_BASE + 7) {
            val = serial_handle_in(port);
        } else if (port == 0x71) {
            val = rtc_handle_in(port);
        } else if (port >= 0xCF8 && port <= 0xCFF) {
            val = pci_handle_in(port, size);
        } else {
             
            switch (port) {
            case 0x61:   
                val = 0x20;
                break;
            case 0x64:   
                val = 0x00;
                break;
            case 0x60:   
                val = 0x00;
                break;
            case 0x20: case 0x21:   
            case 0xA0: case 0xA1:   
                val = 0x00;
                break;
            case 0x40: case 0x41: case 0x42: case 0x43:  
                val = 0x00;
                break;
            case 0x92:   
                val = 0x02;  
                break;
            case 0x80:   
                val = last_post_code;
                break;
            case 0x2F8: case 0x2F9: case 0x2FA: case 0x2FB:  
            case 0x2FC: case 0x2FD: case 0x2FE: case 0x2FF:
            case 0x3E8: case 0x3E9: case 0x3EA: case 0x3EB:  
            case 0x3EC: case 0x3ED: case 0x3EE: case 0x3EF:
                val = 0xFF;  
                break;
            default:
                val = 0xFF;
                break;
            }
        }

        memcpy(run->io_data, &val, size);
    } else {
         
        uint32_t val = 0;
        memcpy(&val, run->io_data, size);

        if (port >= COM1_BASE && port <= COM1_BASE + 7) {
            serial_handle_out(port, (uint8_t)val);
        } else if (port == 0x70 || port == 0x71) {
            rtc_handle_out(port, (uint8_t)val);
        } else if (port >= 0xCF8 && port <= 0xCFF) {
            pci_handle_out(port, val, size);
        } else if (port == 0x80) {
            last_post_code = (uint8_t)val;
        }
         
    }
}

 
void vm_handle_mmio(kvm_run_t *run)
{
     

    if (!run->mmio.is_write) {
         
        memset(run->mmio.data, 0, run->mmio.len);

         
        if (run->mmio.phys_addr >= 0xFEE00000ULL &&
            run->mmio.phys_addr <  0xFEE01000ULL) {
            uint32_t offset = (uint32_t)(run->mmio.phys_addr & 0xFFF);
            uint32_t val = 0;
            switch (offset) {
            case 0x20:   
                val = 0;
                break;
            case 0x30:   
                val = 0x00050014;  
                break;
            case 0x80:   
                val = 0;
                break;
            case 0xD0:   
                val = 0;
                break;
            case 0xE0:   
                val = 0xFFFFFFFF;
                break;
            case 0xF0:   
                val = 0xFF;
                break;
            case 0x100: case 0x110: case 0x120: case 0x130:
            case 0x140: case 0x150: case 0x160: case 0x170:
                 
                val = 0;
                break;
            case 0x180: case 0x190: case 0x1A0: case 0x1B0:
            case 0x1C0: case 0x1D0: case 0x1E0: case 0x1F0:
                 
                val = 0;
                break;
            case 0x200: case 0x210: case 0x220: case 0x230:
            case 0x240: case 0x250: case 0x260: case 0x270:
                 
                val = 0;
                break;
            default:
                val = 0;
                break;
            }
            memcpy(run->mmio.data, &val, 4);
        }
    }
     
}

 
void vm_devices_init(void)
{
    rtc_init();
    serial_ier = 0;
    serial_lcr = 0;
    serial_mcr = 0;
    serial_dll = 0;
    serial_dlh = 0;
    serial_fcr = 0;
    serial_scr = 0;
    pci_config_addr = 0;
    last_post_code = 0;
}
