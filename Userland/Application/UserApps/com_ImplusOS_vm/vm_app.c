#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "Syscalls.h"

static void dbg(const char *msg) { serial_write_string(msg); }
static void dbg_hex32(const char *label, uint32_t v) {
    serial_write_string(label);
    serial_write_string("0x");
    char buf[9];
    for (int i = 0; i < 8; i++) {
        uint8_t n = (v >> ((7 - i) * 4)) & 0xF;
        buf[i] = (n < 10) ? (char)('0' + n) : (char)('A' + n - 10);
    }
    buf[8] = 0;
    serial_write_string(buf);
    serial_write_string(" ");
}
static void dbg_hex64(const char *label, uint64_t v) {
    serial_write_string(label);
    serial_write_string("0x");
    char buf[17];
    for (int i = 0; i < 16; i++) {
        uint8_t n = (v >> ((15 - i) * 4)) & 0xF;
        buf[i] = (n < 10) ? (char)('0' + n) : (char)('A' + n - 10);
    }
    buf[16] = 0;
    serial_write_string(buf);
    serial_write_string(" ");
}
static void dbg_dec(const char *label, int64_t v) {
    serial_write_string(label);
    if (v < 0) { serial_write_string("-"); v = -v; }
    char buf[21]; int i = 20; buf[i] = 0;
    if (v == 0) { buf[--i] = '0'; }
    else { while (v > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; } }
    serial_write_string(&buf[i]);
    serial_write_string(" ");
}

#define KVM_IOCTL_CREATE_VM        0x01
#define KVM_IOCTL_DESTROY_VM       0x02
#define KVM_IOCTL_SET_REGS         0x03
#define KVM_IOCTL_GET_REGS         0x04
#define KVM_IOCTL_MAP_MEMORY       0x05
#define KVM_IOCTL_RUN              0x06
#define KVM_IOCTL_GET_EXIT_INFO    0x08
#define KVM_IOCTL_SET_IO_RESPONSE  0x09

#define EXIT_EXCEPTION_NMI  0
#define EXIT_EXT_INT        1
#define EXIT_TRIPLE_FAULT   2
#define EXIT_CPUID          10
#define EXIT_HLT            12
#define EXIT_IO             30
#define EXIT_MSR_READ       31
#define EXIT_MSR_WRITE      32
#define EXIT_CR_ACCESS      28
#define EXIT_EPT_VIOLATION  48
#define EXIT_EPT_MISCONFIG  49
#define EXIT_XSETBV         55

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags, cr0, cr3, cr4;
    uint16_t cs, ds, es, fs, gs, ss, tr, ldtr;
    uint32_t _pad; uint32_t cs_base;
} vm_regs_t;

typedef struct {
    uint64_t guest_phys_addr, host_virt_addr, size;
    uint32_t flags, _pad;
} vm_memory_region_t;

typedef struct {
    uint32_t exit_reason, _pad0;
    uint64_t exit_qualification;
    uint16_t io_port;
    uint8_t  io_size, io_direction;
    uint32_t io_data;
    uint64_t guest_phys_addr, guest_linear_addr, guest_rip;
    uint32_t instruction_length, _pad1;
} vm_exit_info_t;

extern int32_t kvm_open(void);
extern int64_t kvm_ioctl(int32_t fd, uint64_t request, uint64_t arg);
extern int32_t kvm_close(int32_t fd);

#define COM1_PORT   0x3F8
#define COM1_LSR    0x3FD
#define GUEST_RAM_SIZE  (256ULL * 1024ULL * 1024ULL)
#define FB_W 800
#define FB_H 600

#define VBE_DISPI_INDEX_PORT  0x01CE
#define VBE_DISPI_DATA_PORT   0x01CF
#define VBE_DISPI_ID          0x00
#define VBE_DISPI_XRES        0x01
#define VBE_DISPI_YRES        0x02
#define VBE_DISPI_BPP         0x03
#define VBE_DISPI_ENABLE      0x04
#define VBE_DISPI_BANK        0x05
#define VBE_DISPI_VIRT_WIDTH  0x06
#define VBE_DISPI_VIRT_HEIGHT 0x07
#define VBE_DISPI_X_OFFSET    0x08
#define VBE_DISPI_Y_OFFSET    0x09
#define VBE_DISPI_VIDEO_MEM   0x0A
#define GUEST_FB_GPA          0xE0000000ULL
#define GUEST_FB_SIZE         (16ULL * 1024ULL * 1024ULL)

#define CMOS_ADDR_PORT  0x70
#define CMOS_DATA_PORT  0x71
#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PIC1_CMD        0x20
#define PIC1_DATA       0x21
#define PIC2_CMD        0xA0
#define PIC2_DATA       0xA1
#define PIT_CH0         0x40
#define PIT_CMD         0x43
#define PS2_DATA        0x60
#define PS2_STATUS      0x64
#define PORT_A20        0x92
#define POST_PORT       0x80
#define PORT_SYSCTRL_B  0x61
#define PORT_RESET_CTRL 0xCF9
#define PORT_IO_DELAY   0xED
#define GUEST_LAPIC_GPA 0xFEE00000ULL
#define OVMF_DEBUG_PORT 0x402
#define GUEST_IOAPIC_GPA 0xFEC00000ULL

#define FW_CFG_PORT_SEL   0x510
#define FW_CFG_PORT_DATA  0x511

#define DUMMY_PAGE_MAX  512

static const uint32_t vga_pal[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
    0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
    0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF,
};

static int vga_cursor_x = 0;
static int vga_cursor_y = 0;

static uint8_t  cmos_index = 0;
static uint32_t pci_config_addr = 0;
static uint16_t pit_counter = 0;
static uint8_t  vga_shadow[4000];
static bool     vga_shadow_init = false;
static uint16_t last_io_port = 0;
static uint32_t io_port_repeat_count = 0;
static char     ovmf_debug_buf[256];
static int      ovmf_debug_pos = 0;

static void    *dummy_pages[DUMMY_PAGE_MAX];
static uint64_t dummy_gpas[DUMMY_PAGE_MAX];
static uint32_t dummy_page_count = 0;
static void    *dummy_fallback = NULL;

static uint16_t bga_index = 0;
static uint16_t bga_xres = 640, bga_yres = 480, bga_bpp = 32;
static uint16_t bga_enabled = 0;
static uint16_t bga_virt_w = 640, bga_virt_h = 480;
static uint16_t bga_x_off = 0, bga_y_off = 0;
static void    *guest_fb_host = NULL;
static uint8_t  last_post_code = 0;
static uint8_t  port61_val = 0x20;
static void    *lapic_page_host = NULL;

#define PS2_OUTBUF_MAX 16
static uint8_t  ps2_outbuf[PS2_OUTBUF_MAX];
static uint8_t  ps2_outbuf_head = 0, ps2_outbuf_tail = 0;
static uint8_t  ps2_config = 0x47;
static uint8_t  ps2_cmd_pending = 0;
static uint8_t  ps2_dev_cmd_pending = 0;

static void ps2_enqueue(uint8_t val) {
    uint8_t next = (uint8_t)((ps2_outbuf_head + 1) % PS2_OUTBUF_MAX);
    if (next != ps2_outbuf_tail) {
        ps2_outbuf[ps2_outbuf_head] = val;
        ps2_outbuf_head = next;
    }
}
static int ps2_has_data(void) {
    return ps2_outbuf_head != ps2_outbuf_tail;
}
static uint8_t ps2_dequeue(void) {
    if (!ps2_has_data()) return 0;
    uint8_t val = ps2_outbuf[ps2_outbuf_tail];
    ps2_outbuf_tail = (uint8_t)((ps2_outbuf_tail + 1) % PS2_OUTBUF_MAX);
    return val;
}

static void ps2_handle_controller_cmd(uint8_t cmd) {
    switch (cmd) {
    case 0x20: ps2_enqueue(ps2_config); break;
    case 0x60: ps2_cmd_pending = 0x60; break;
    case 0xA7: break;
    case 0xA8: break;
    case 0xA9: ps2_enqueue(0x00); break;
    case 0xAA: ps2_enqueue(0x55); break;
    case 0xAB: ps2_enqueue(0x00); break;
    case 0xAD: break;
    case 0xAE: break;
    case 0xD4: ps2_cmd_pending = 0xD4; break;
    default: break;
    }
}

static void ps2_handle_data_write(uint8_t data) {
    if (ps2_cmd_pending == 0x60) {
        ps2_config = data;
        ps2_cmd_pending = 0;
    } else if (ps2_cmd_pending == 0xD4) {
        ps2_enqueue(0xFA);
        ps2_cmd_pending = 0;
    } else if (ps2_dev_cmd_pending == 0xED) {
        ps2_enqueue(0xFA);
        ps2_dev_cmd_pending = 0;
    } else {
        switch (data) {
        case 0xED: ps2_enqueue(0xFA); ps2_dev_cmd_pending = 0xED; break;
        case 0xF2: ps2_enqueue(0xFA); ps2_enqueue(0xAB); ps2_enqueue(0x83); break;
        case 0xF3: ps2_enqueue(0xFA); ps2_dev_cmd_pending = 0xF3; break;
        case 0xF4: ps2_enqueue(0xFA); break;
        case 0xF5: ps2_enqueue(0xFA); break;
        case 0xF6: ps2_enqueue(0xFA); break;
        case 0xFF: ps2_enqueue(0xFA); ps2_enqueue(0xAA); break;
        default:   ps2_enqueue(0xFA); break;
        }
    }
}

#define ACPI_PMBA       0x600
#define ACPI_PM_TMR     (ACPI_PMBA + 0x08)
#define ACPI_PM1_STS    (ACPI_PMBA + 0x00)
#define ACPI_PM1_EN     (ACPI_PMBA + 0x02)
#define ACPI_PM1_CNT    (ACPI_PMBA + 0x04)

static uint32_t acpi_pm_last_ticks = 0;

static uint32_t acpi_pm_timer_read(void) {
    uint64_t ms = get_uptime_ms();
    uint32_t ticks = (uint32_t)(ms * 3580);
    if (ticks > acpi_pm_last_ticks) {
        acpi_pm_last_ticks = ticks;
    } else {
        acpi_pm_last_ticks += 1000;
    }
    return acpi_pm_last_ticks & 0x00FFFFFF;
}

static uint32_t pci_cfg_read(uint32_t addr) {
    uint32_t bus = (addr >> 16) & 0xFF;
    uint32_t dev = (addr >> 11) & 0x1F;
    uint32_t fun = (addr >> 8) & 0x7;
    uint32_t reg = addr & 0xFC;
    if (bus != 0) return 0xFFFFFFFF;

    if (dev == 0 && fun == 0) {
        switch (reg & 0xFC) {
        case 0x00: return 0x12378086;
        case 0x04: return 0x00000006;
        case 0x08: return 0x06000002;
        case 0x0C: return 0x00000000;
        default:   return 0x00000000;
        }
    }
    if (dev == 1 && fun == 0) {
        switch (reg & 0xFC) {
        case 0x00: return 0x70008086;
        case 0x04: return 0x00000007;
        case 0x08: return 0x06010000;
        default:   return 0x00000000;
        }
    }
    if (dev == 1 && fun == 3) {
        switch (reg & 0xFC) {
        case 0x00: return 0x71138086;
        case 0x04: return 0x00000001;
        case 0x08: return 0x06800000;
        case 0x40: return ACPI_PMBA | 1;
        case 0x44: return 0x01;
        case 0x80: return 0x01;
        default:   return 0x00000000;
        }
    }
    if (dev == 2 && fun == 0) {
        switch (reg & 0xFC) {
        case 0x00: return 0x11111234;
        case 0x04: return 0x00000003;
        case 0x08: return 0x03000002;
        case 0x0C: return 0x00000000;
        case 0x10: return (uint32_t)(GUEST_FB_GPA | 0x08);
        case 0x2C: return 0x11111234;
        case 0x3C: return 0x00000100;
        default:   return 0x00000000;
        }
    }
    return 0xFFFFFFFF;
}

static uint16_t bga_read(uint16_t index) {
    switch (index) {
    case VBE_DISPI_ID:         return 0xB0C5;
    case VBE_DISPI_XRES:       return bga_xres;
    case VBE_DISPI_YRES:       return bga_yres;
    case VBE_DISPI_BPP:        return bga_bpp;
    case VBE_DISPI_ENABLE:     return bga_enabled;
    case VBE_DISPI_VIRT_WIDTH: return bga_virt_w;
    case VBE_DISPI_VIRT_HEIGHT:return bga_virt_h;
    case VBE_DISPI_X_OFFSET:   return bga_x_off;
    case VBE_DISPI_Y_OFFSET:   return bga_y_off;
    case VBE_DISPI_VIDEO_MEM:  return (uint16_t)(GUEST_FB_SIZE / (64*1024));
    default: return 0;
    }
}

static void bga_write(uint16_t index, uint16_t val) {
    switch (index) {
    case VBE_DISPI_XRES:       bga_xres = val; bga_virt_w = val; break;
    case VBE_DISPI_YRES:       bga_yres = val; bga_virt_h = val; break;
    case VBE_DISPI_BPP:        bga_bpp = val; break;
    case VBE_DISPI_ENABLE:
        bga_enabled = val;
        break;
    case VBE_DISPI_VIRT_WIDTH: bga_virt_w = val; break;
    case VBE_DISPI_VIRT_HEIGHT:bga_virt_h = val; break;
    case VBE_DISPI_X_OFFSET:   bga_x_off = val; break;
    case VBE_DISPI_Y_OFFSET:   bga_y_off = val; break;
    default: break;
    }
}

static uint8_t bin_to_bcd(uint8_t val) {
    return (uint8_t)(((val / 10) << 4) | (val % 10));
}

static uint8_t cmos_read(uint8_t reg) {
    uint32_t ext_kb = (uint32_t)((GUEST_RAM_SIZE / 1024) - 1024);
    if (ext_kb > 15360) ext_kb = 15360;
    uint32_t above16_64k = 0;
    if (GUEST_RAM_SIZE > 16ULL * 1024ULL * 1024ULL)
        above16_64k = (uint32_t)((GUEST_RAM_SIZE - 16ULL * 1024ULL * 1024ULL) / (64ULL * 1024ULL));
    if (above16_64k > 0xFFFF) above16_64k = 0xFFFF;

    uint64_t ms = get_uptime_ms();
    uint64_t total_sec = 1735689600ULL + ms / 1000;
    uint8_t sec  = (uint8_t)(total_sec % 60);
    uint8_t min  = (uint8_t)((total_sec / 60) % 60);
    uint8_t hour = (uint8_t)((total_sec / 3600) % 24);
    uint32_t days = (uint32_t)(total_sec / 86400);
    uint8_t dow  = (uint8_t)((4 + days) % 7 + 1);

    switch (reg) {
    case 0x00: return bin_to_bcd(sec);
    case 0x02: return bin_to_bcd(min);
    case 0x04: return bin_to_bcd(hour);
    case 0x06: return dow;
    case 0x07: return bin_to_bcd(1);
    case 0x08: return bin_to_bcd(1);
    case 0x09: return bin_to_bcd(26);
    case 0x0A: return 0x26;
    case 0x0B: return 0x02;
    case 0x0C: return 0x00;
    case 0x0D: return 0x80;
    case 0x0E: return 0x00;
    case 0x10: return 0x00;
    case 0x14: return 0x40;
    case 0x15: return (uint8_t)(640 & 0xFF);
    case 0x16: return (uint8_t)((640 >> 8) & 0xFF);
    case 0x17: return (uint8_t)(ext_kb & 0xFF);
    case 0x18: return (uint8_t)((ext_kb >> 8) & 0xFF);
    case 0x30: return (uint8_t)(ext_kb & 0xFF);
    case 0x31: return (uint8_t)((ext_kb >> 8) & 0xFF);
    case 0x34: return (uint8_t)(above16_64k & 0xFF);
    case 0x35: return (uint8_t)((above16_64k >> 8) & 0xFF);
    case 0x32: return bin_to_bcd(20);
    default:   return 0x00;
    }
}

static uint32_t handle_io_in(uint16_t port) {
    if (port >= COM1_PORT && port <= (COM1_PORT + 7)) {
        if (port == COM1_LSR) return 0x60;
        return 0x00;
    }
    switch (port) {
    case CMOS_DATA_PORT:  return cmos_read(cmos_index);
    case PCI_CONFIG_ADDR: return pci_config_addr;
    case PCI_CONFIG_DATA:
    case PCI_CONFIG_DATA + 1:
    case PCI_CONFIG_DATA + 2:
    case PCI_CONFIG_DATA + 3: {
        int off = port - PCI_CONFIG_DATA;
        uint32_t val = pci_cfg_read(pci_config_addr);
        if (off) val >>= (off * 8);
        return val;
    }
    case PIC1_CMD:        return 0x00;
    case PIC1_DATA:       return 0xFF;
    case PIC2_CMD:        return 0x00;
    case PIC2_DATA:       return 0xFF;
    case PIT_CH0:         return (uint32_t)(pit_counter++ & 0xFF);
    case 0x41: case 0x42: return 0x00;
    case PS2_DATA:        return ps2_dequeue();
    case PS2_STATUS:
        return (uint8_t)((ps2_has_data() ? 0x01 : 0x00) | 0x04);
    case PORT_A20:        return 0x02;
    case POST_PORT:       return 0x00;
    case PORT_SYSCTRL_B:
        port61_val ^= 0x20;
        return port61_val;
    case PORT_RESET_CTRL: return 0x00;
    case PORT_IO_DELAY:   return 0x00;

    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x08: case 0x0D: case 0x0F:
        return 0x00;

    case 0xC0: case 0xC2: case 0xC4: case 0xC6:
    case 0xC8: case 0xCA: case 0xCC: case 0xCE:
    case 0xD0: case 0xD2: case 0xD4: case 0xD6:
    case 0xDA: case 0xDE:
        return 0x00;

    case 0x3DA:           return 0x00;
    case 0x3D4: case 0x3D5:
    case 0x3C0: case 0x3C1: case 0x3C2: case 0x3C3:
    case 0x3C4: case 0x3C5: case 0x3C6: case 0x3C7:
    case 0x3C8: case 0x3C9: case 0x3CA: case 0x3CB:
    case 0x3CC: case 0x3CD: case 0x3CE: case 0x3CF:
        return 0x00;
    case VBE_DISPI_INDEX_PORT: return bga_index;
    case VBE_DISPI_DATA_PORT:  return bga_read(bga_index);
    case ACPI_PM_TMR:     return acpi_pm_timer_read();
    case ACPI_PM1_STS:    return 0x0000;
    case ACPI_PM1_EN:     return 0x0000;
    case ACPI_PM1_CNT:    return 0x0000;
    case OVMF_DEBUG_PORT: return 0x00;
    case FW_CFG_PORT_SEL: return 0x00;
    case FW_CFG_PORT_DATA:return 0x00;
    default:
        return 0x00;
    }
}

static void handle_io_out(uint16_t port, uint32_t data) {
    if (port >= COM1_PORT && port <= (COM1_PORT + 7)) {
        return;
    }
    switch (port) {
    case CMOS_ADDR_PORT:  cmos_index = (uint8_t)(data & 0x7F); break;
    case PCI_CONFIG_ADDR: pci_config_addr = data;              break;
    case PCI_CONFIG_DATA:
    case PCI_CONFIG_DATA + 1:
    case PCI_CONFIG_DATA + 2:
    case PCI_CONFIG_DATA + 3:
        break;
    case POST_PORT:
        if ((uint8_t)data != last_post_code) {
            last_post_code = (uint8_t)data;
            dbg("[POST] "); dbg_hex32("", (uint32_t)last_post_code); dbg("\n");
        }
        break;
    case VBE_DISPI_INDEX_PORT: bga_index = (uint16_t)(data & 0xFFFF); break;
    case VBE_DISPI_DATA_PORT:  bga_write(bga_index, (uint16_t)(data & 0xFFFF)); break;
    case PIC1_CMD: case PIC1_DATA:
    case PIC2_CMD: case PIC2_DATA:
    case PIT_CH0:  case 0x41: case 0x42: case PIT_CMD:
        break;
    case PS2_STATUS: ps2_handle_controller_cmd((uint8_t)(data & 0xFF)); break;
    case PS2_DATA:   ps2_handle_data_write((uint8_t)(data & 0xFF)); break;
    case PORT_A20:
    case PORT_SYSCTRL_B:
    case PORT_RESET_CTRL:
    case PORT_IO_DELAY:
    case ACPI_PM1_STS: case ACPI_PM1_EN: case ACPI_PM1_CNT:
    case FW_CFG_PORT_SEL: case FW_CFG_PORT_DATA:
    case OVMF_DEBUG_PORT: {
        char c = (char)(data & 0xFF);
        if (ovmf_debug_pos < 255) {
            ovmf_debug_buf[ovmf_debug_pos++] = c;
            if (c == '\n' || ovmf_debug_pos >= 254) {
                ovmf_debug_buf[ovmf_debug_pos] = 0;
                serial_write_string(ovmf_debug_buf);
                ovmf_debug_pos = 0;
            }
        }
        break;
    }
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: case 0x0E: case 0x0F:

    case 0xC0: case 0xC2: case 0xC4: case 0xC6:
    case 0xC8: case 0xCA: case 0xCC: case 0xCE:
    case 0xD0: case 0xD2: case 0xD4: case 0xD6:
    case 0xD8: case 0xDA: case 0xDC: case 0xDE:

    case 0x3C0: case 0x3C1: case 0x3C2: case 0x3C3:
    case 0x3C4: case 0x3C5: case 0x3C6: case 0x3C7:
    case 0x3C8: case 0x3C9: case 0x3CA: case 0x3CB:
    case 0x3CC: case 0x3CD: case 0x3CE: case 0x3CF:
    case 0x3D4: case 0x3D5: case 0x3DA:
        break;
    default: break;
    }
}

static void vga_putc(uint8_t *ram, char c) {
    uint8_t *vga = ram + 0xB8000;
    if (c == '\n') {
        vga_cursor_x = 0;
        vga_cursor_y++;
    } else if (c == '\r') {
        vga_cursor_x = 0;
    } else if (c == '\b') {
        if (vga_cursor_x > 0) vga_cursor_x--;
        vga[(vga_cursor_y * 80 + vga_cursor_x) * 2] = ' ';
        vga[(vga_cursor_y * 80 + vga_cursor_x) * 2 + 1] = 0x07;
    } else {
        vga[(vga_cursor_y * 80 + vga_cursor_x) * 2] = c;
        vga[(vga_cursor_y * 80 + vga_cursor_x) * 2 + 1] = 0x07;
        vga_cursor_x++;
        if (vga_cursor_x >= 80) {
            vga_cursor_x = 0;
            vga_cursor_y++;
        }
    }
    if (vga_cursor_y >= 25) {
        memmove(vga, vga + 80 * 2, 80 * 24 * 2);
        memset(vga + 80 * 24 * 2, 0, 80 * 2);
        vga_cursor_y = 24;
    }
}

static void render_vga_text(window_id_t wid, const uint8_t *ram)
{
    static uint16_t vga_cache[80 * 25];
    static bool cache_initialized = false;
    if (!cache_initialized) {
        memset(vga_cache, 0xFF, sizeof(vga_cache));
        cache_initialized = true;
    }

    const uint8_t *vga = ram + 0xB8000;
    if (vga_shadow_init && memcmp(vga_shadow, vga, 4000) == 0) return;
    memcpy(vga_shadow, vga, 4000);
    vga_shadow_init = true;

    for (uint32_t row = 0; row < 25; row++) {
        for (uint32_t col = 0; col < 80; col++) {
            uint32_t off = (row * 80 + col) * 2;
            uint8_t ch   = vga[off];
            uint8_t attr = vga[off + 1];
            uint16_t val = (uint16_t)((attr << 8) | ch);

            if (vga_cache[row * 80 + col] == val) continue;
            vga_cache[row * 80 + col] = val;

            uint32_t bg = vga_pal[(attr >> 4) & 0x0F];
            uint32_t fg = vga_pal[attr & 0x0F];
            uint32_t x = col * 8, y = row * 16;

            if (bg != 0xFF000000) {
                draw_fill_rect(x, y, 8, 16, bg);
            } else {
                draw_fill_rect(x, y, 8, 16, bg);
            }
            if (ch >= 0x20 && ch <= 0x7E) {
                char t[2] = { (char)ch, 0 };
                window_draw_text(wid, x, y, t, fg, 12.0f);
            }
        }
    }
}

static void render_gop_fb(void)
{
    if (!guest_fb_host || !bga_enabled) return;
    uint32_t *fb = (uint32_t *)guest_fb_host;
    uint32_t w = bga_xres, h = bga_yres;
    if (w > FB_W) w = FB_W;
    if (h > FB_H) h = FB_H;
    uint32_t stride = bga_virt_w;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t pixel = fb[(y + bga_y_off) * stride + (x + bga_x_off)];
            uint32_t argb = 0xFF000000 | pixel;
            draw_pixel(x, y, argb);
        }
    }
}

void _start(void)
{
    int32_t kvm_fd = kvm_open();
    if (kvm_fd < 0) {
        process_exit(-1);
        return;
    }

    int64_t vm_id = kvm_ioctl(kvm_fd, KVM_IOCTL_CREATE_VM, 0);
    if (vm_id < 0) {
        kvm_close(kvm_fd);
        process_exit(-1);
        return;
    }

    void *guest_ram = os_mmap(GUEST_RAM_SIZE, 0);
    if (!guest_ram) {
        kvm_close(kvm_fd);
        process_exit(-1);
        return;
    }
    memset(guest_ram, 0, GUEST_RAM_SIZE);

    vm_memory_region_t region = {
        .guest_phys_addr = 0,
        .host_virt_addr  = (uint64_t)(uintptr_t)guest_ram,
        .size            = GUEST_RAM_SIZE,
        .flags = 0, ._pad = 0
    };
    if (kvm_ioctl(kvm_fd, KVM_IOCTL_MAP_MEMORY, (uint64_t)(uintptr_t)&region) < 0) {
        kvm_close(kvm_fd);
        process_exit(-1);
        return;
    }

    guest_fb_host = os_mmap(GUEST_FB_SIZE, 0);
    if (guest_fb_host) {
        memset(guest_fb_host, 0, GUEST_FB_SIZE);
        vm_memory_region_t fb_region = {
            .guest_phys_addr = GUEST_FB_GPA,
            .host_virt_addr  = (uint64_t)(uintptr_t)guest_fb_host,
            .size            = GUEST_FB_SIZE,
            .flags           = 0, ._pad = 0
        };
        kvm_ioctl(kvm_fd, KVM_IOCTL_MAP_MEMORY, (uint64_t)(uintptr_t)&fb_region);
    }

    lapic_page_host = os_mmap(4096, 0);
    if (lapic_page_host) {
        memset(lapic_page_host, 0, 4096);
        uint32_t *lapic = (uint32_t *)lapic_page_host;
        lapic[0x020/4] = 0x00000000;
        lapic[0x030/4] = 0x00050014;
        lapic[0x080/4] = 0x00000000;
        lapic[0x0D0/4] = 0x01000000;
        lapic[0x0E0/4] = 0xFFFFFFFF;
        lapic[0x0F0/4] = 0x000001FF;
        lapic[0x320/4] = 0x00010000;
        lapic[0x350/4] = 0x00010000;
        lapic[0x360/4] = 0x00010000;
        vm_memory_region_t lapic_region = {
            .guest_phys_addr = GUEST_LAPIC_GPA,
            .host_virt_addr  = (uint64_t)(uintptr_t)lapic_page_host,
            .size            = 4096,
            .flags           = 0, ._pad = 0
        };
        kvm_ioctl(kvm_fd, KVM_IOCTL_MAP_MEMORY, (uint64_t)(uintptr_t)&lapic_region);
    }

    void *ioapic_page = os_mmap(4096, 0);
    if (ioapic_page) {
        memset(ioapic_page, 0, 4096);
        uint32_t *ioapic = (uint32_t *)ioapic_page;
        ioapic[0x00/4] = 0x00;
        ioapic[0x10/4] = 0x00170020;
        vm_memory_region_t ioapic_region = {
            .guest_phys_addr = GUEST_IOAPIC_GPA,
            .host_virt_addr  = (uint64_t)(uintptr_t)ioapic_page,
            .size            = 4096,
            .flags           = 0, ._pad = 0
        };
        kvm_ioctl(kvm_fd, KVM_IOCTL_MAP_MEMORY, (uint64_t)(uintptr_t)&ioapic_region);
    }

    dbg("\n=== ImplusOS VM: OVMF Boot Debug ===\n");

    int using_ovmf = 0;
    void *ovmf_ram = NULL;
    uint64_t ovmf_map_size = 4ULL * 1024ULL * 1024ULL;

    int32_t ovmf_fd = file_open("/Userland/UserApps/com_ImplusOS_vm/Resource/OVMF_CODE.fd", 0);
    dbg_dec("[OVMF] CODE fd=", ovmf_fd); dbg("\n");
    if (ovmf_fd >= 0) {
        ovmf_ram = os_mmap(ovmf_map_size, 0);
        dbg_hex64("[OVMF] ovmf_ram=", (uint64_t)(uintptr_t)ovmf_ram); dbg("\n");
        if (ovmf_ram) {
            memset(ovmf_ram, 0xFF, ovmf_map_size);

            void *tmp_buf = os_mmap(ovmf_map_size, 0);
            int64_t total = 0, chunk;
            if (tmp_buf) {
                while ((chunk = file_read(ovmf_fd, (uint8_t *)tmp_buf + total,
                        (uint32_t)(ovmf_map_size - (uint64_t)total))) > 0) {
                    total += chunk;
                }
            }
            dbg_dec("[OVMF] CODE bytes read=", total); dbg("\n");
            if (total > 0) {
                uint64_t code_offset = ovmf_map_size - (uint64_t)total;
                dbg_hex64("[OVMF] code_offset=", code_offset); dbg("\n");
                memcpy((uint8_t *)ovmf_ram + code_offset, tmp_buf, (size_t)total);

                vm_memory_region_t rom_region = {
                    .guest_phys_addr = 0xFFC00000,
                    .host_virt_addr  = (uint64_t)(uintptr_t)ovmf_ram,
                    .size            = ovmf_map_size,
                    .flags           = 0, ._pad = 0
                };
                int64_t map_rc = kvm_ioctl(kvm_fd, KVM_IOCTL_MAP_MEMORY, (uint64_t)(uintptr_t)&rom_region);
                dbg_dec("[OVMF] CODE map rc=", map_rc); dbg("\n");
                if (map_rc == 0) {
                    using_ovmf = 1;
                }
                
                uint8_t *rv = (uint8_t *)ovmf_ram + ovmf_map_size - 16;
                dbg("[OVMF] Reset vector bytes: ");
                for (int i = 0; i < 16; i++) {
                    dbg_hex32("", rv[i]);
                }
                dbg("\n");
            }
        }
        file_close(ovmf_fd);
    }

    if (using_ovmf && ovmf_ram) {
        int32_t vars_fd = file_open(
            "/Userland/UserApps/com_ImplusOS_vm/Resource/OVMF_VARS.fd", 0);
        dbg_dec("[OVMF] VARS fd=", vars_fd); dbg("\n");
        if (vars_fd >= 0) {
            int64_t vt = 0, vc;
            while ((vc = file_read(vars_fd, (uint8_t *)ovmf_ram + vt,
                    (uint32_t)(ovmf_map_size - (uint64_t)vt))) > 0) {
                vt += vc;
            }
            dbg_dec("[OVMF] VARS bytes read=", vt); dbg("\n");
            file_close(vars_fd);
        } else {
            dbg("[OVMF] WARNING: OVMF_VARS.fd not found!\n");
        }
    }
    dbg_dec("[OVMF] using_ovmf=", using_ovmf); dbg("\n");

    vm_regs_t regs;
    memset(&regs, 0, sizeof(regs));
    if (using_ovmf) {
        regs.rip = 0xFFF0;
        regs.cs  = 0xF000;
        regs.cs_base = 0xFFFF0000;
        regs.ds  = 0;
        regs.es  = 0;
        regs.ss  = 0;
        regs.fs  = 0;
        regs.gs  = 0;
    } else {
        regs.rip = 0x7C00;
        regs.cs  = 0;
        regs.cs_base = 0;
    }
    regs.rflags = 0x2;
    regs.rsp    = 0;
    regs.cr0    = 0x60000010;
    kvm_ioctl(kvm_fd, KVM_IOCTL_SET_REGS, (uint64_t)(uintptr_t)&regs);

    dbg_hex64("[REGS] RIP=", regs.rip);
    dbg_hex32("[REGS] CS=", regs.cs);
    dbg_hex64("[REGS] CS_BASE=", regs.cs_base);
    dbg_hex64("[REGS] CR0=", regs.cr0);
    dbg_hex64("[REGS] RSP=", regs.rsp);
    dbg("\n");
    dbg("[VM] Entering run loop...\n");

    window_id_t wid = window_create_ex(50, 50, FB_W, FB_H, 0xFF000000, "ImplusOS VM");
    if (wid) {
        window_show(wid);
        window_subscribe_keyboard(wid);
        graphics_init(wid);
        draw_fill_rect(0, 0, FB_W, FB_H, 0xFF000000);
        draw_present();
    }

    int running = 1;
    uint32_t tick = 0, hlt_n = 0;
    uint64_t last_render_time = get_uptime_ms();

    uint32_t unhandled_exit_count = 0;
    uint32_t dbg_total_exits = 0;
    while (running) {
        int64_t rc = kvm_ioctl(kvm_fd, KVM_IOCTL_RUN, 0);
        if (rc < 0) {
            dbg("[VM] KVM_RUN failed!\n");
            break;
        }

        vm_exit_info_t ei;
        kvm_ioctl(kvm_fd, KVM_IOCTL_GET_EXIT_INFO, (uint64_t)(uintptr_t)&ei);

        dbg_total_exits++;
        
        switch (ei.exit_reason) {
        case EXIT_IO:
            hlt_n = 0;
            if (ei.io_direction == 0) {
                if (ei.io_port == COM1_PORT) {
                    vga_putc((uint8_t *)guest_ram, (char)(ei.io_data & 0xFF));
                }
                handle_io_out(ei.io_port, ei.io_data);
            } else {
                uint32_t d = handle_io_in(ei.io_port);
                if (ei.io_size == 1) d &= 0xFF;
                else if (ei.io_size == 2) d &= 0xFFFF;
                kvm_ioctl(kvm_fd, KVM_IOCTL_SET_IO_RESPONSE, (uint64_t)d);
            }
            if (ei.io_port == last_io_port) {
                if (++io_port_repeat_count > 5000) {
                    process_yield();
                    io_port_repeat_count = 0;
                }
            } else {
                last_io_port = ei.io_port;
                io_port_repeat_count = 0;
            }
            break;
        case EXIT_HLT:
            if (++hlt_n > 50) {
                hlt_n = 0;
                sleep_ms(10);
            } else {
                process_yield();
            }
            break;
        case EXIT_CPUID: case EXIT_MSR_READ: case EXIT_MSR_WRITE:
        case EXIT_CR_ACCESS: case EXIT_XSETBV:
            hlt_n = 0;
            break;
        case EXIT_TRIPLE_FAULT:
            dbg("[TRIPLE FAULT] VM stopped!\n");
            running = 0;
            break;
        case EXIT_EPT_VIOLATION:
        case EXIT_EPT_MISCONFIG:
            {
                uint64_t fault_gpa = ei.guest_phys_addr & ~0xFFFULL;
                void *page = NULL;
                if (dummy_page_count < DUMMY_PAGE_MAX) {
                    page = os_mmap(4096, 0);
                    if (page) {
                        memset(page, 0, 4096);
                        dummy_pages[dummy_page_count] = page;
                        dummy_gpas[dummy_page_count]  = fault_gpa;
                        dummy_page_count++;
                    }
                }
                if (!page) {
                    if (!dummy_fallback) {
                        dummy_fallback = os_mmap(4096, 0);
                        if (dummy_fallback) memset(dummy_fallback, 0, 4096);
                    }
                    page = dummy_fallback;
                }
                if (page) {
                    vm_memory_region_t dummy_region = {
                        .guest_phys_addr = fault_gpa,
                        .host_virt_addr  = (uint64_t)(uintptr_t)page,
                        .size            = 4096,
                        .flags           = 0, ._pad = 0
                    };
                    kvm_ioctl(kvm_fd, KVM_IOCTL_MAP_MEMORY, (uint64_t)(uintptr_t)&dummy_region);
                }
            }
            break;
        default:
            if (dbg_total_exits < 100) {
                dbg("[EXIT] "); dbg_hex32("reason=", ei.exit_reason); dbg("\n");
            }
            break;
        }

        if ((++tick & 0xFFFF) == 0) {
            if (wid) {
                uint64_t now = get_uptime_ms();
                if (now - last_render_time >= 100) {
                    if (bga_enabled && guest_fb_host) render_gop_fb();
                    else render_vga_text(wid, (const uint8_t *)guest_ram);
                    draw_present();
                    last_render_time = now;
                }
            }
            if ((tick & 0xFFFFF) == 0) {
                dbg("[SUMMARY] exits="); dbg_dec("", dbg_total_exits); dbg("\n");
            }
        }
    }

    if (wid) {
        if (bga_enabled && guest_fb_host) {
            render_gop_fb();
        } else {
            render_vga_text(wid, (const uint8_t *)guest_ram);
        }
        draw_present();
    }

    kvm_ioctl(kvm_fd, KVM_IOCTL_DESTROY_VM, 0);
    kvm_close(kvm_fd);

    process_exit(0);
}