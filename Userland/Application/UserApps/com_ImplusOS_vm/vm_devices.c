/* vm_devices.c — Device emulation for com_ImplusOS_vm
 *
 * Emulates: COM1 serial, RTC, PCI config, 8042 PS/2 controller,
 *           PIT, PIC, LAPIC (MMIO).
 * Serial output is displayed in an ImplusOS window as a text console.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/syscalls.h>
#include "../../../Syscalls.h"
#include "../../../API/Serial.h"
#include "vm_devices.h"

/* ── Console scrollback buffer ─────────────────────────────────── */
#define CON_COLS     100
#define CON_ROWS     80
#define CON_VISIBLE  34

static char     con_lines[CON_ROWS][CON_COLS];
static uint32_t con_cur_row  = 0;
static uint32_t con_cur_col  = 0;
static uint32_t con_total    = 1;   /* total lines written */
static uint32_t con_dirty    = 0;
static uint32_t con_win_id   = 0;
static uint64_t serial_char_count = 0;

/* Host serial echo — line buffer for printf */
#define HOST_LINE_SIZE 200
static char     host_line[HOST_LINE_SIZE];
static uint32_t host_line_pos = 0;

static void host_serial_putchar(char c)
{
    if (c == '\n' || host_line_pos >= HOST_LINE_SIZE - 1) {
        host_line[host_line_pos] = '\n';
        host_line[host_line_pos+1] = '\0';
        serial_write_string(host_line);
        host_line_pos = 0;
    } else if (c >= ' ' || c == '\t') {
        host_line[host_line_pos++] = c;
    }
}

/* ANSI escape sequence mini-parser state */
static uint8_t  esc_state = 0;     /* 0=normal 1=got ESC 2=got CSI */
static char     esc_buf[16];
static uint8_t  esc_len = 0;

static void con_newline(void)
{
    con_cur_col = 0;
    con_cur_row = (con_cur_row + 1) % CON_ROWS;
    con_total++;
    memset(con_lines[con_cur_row], 0, CON_COLS);
    con_dirty = 1;
}

static void con_putchar(char c)
{
    if (esc_state == 1) {
        if (c == '[') { esc_state = 2; esc_len = 0; return; }
        esc_state = 0;  /* not CSI, ignore */
        return;
    }
    if (esc_state == 2) {
        /* Accumulate CSI parameters; finish on letter */
        if ((c >= '0' && c <= '9') || c == ';') {
            if (esc_len < sizeof(esc_buf) - 1) esc_buf[esc_len++] = c;
            return;
        }
        /* End of CSI sequence */
        esc_state = 0;
        if (c == 'J' || c == 'K' || c == 'H' || c == 'm') {
            /* Ignore color/cursor/clear codes — just consume */
        }
        return;
    }

    if (c == '\033') { esc_state = 1; return; }
    if (c == '\n')   { con_newline(); return; }
    if (c == '\r')   { con_cur_col = 0; return; }
    if (c == '\t')   { con_cur_col = (con_cur_col + 4) & ~3u; if (con_cur_col >= CON_COLS - 1) con_newline(); return; }
    if (c == '\b')   { if (con_cur_col > 0) con_cur_col--; return; }

    if (con_cur_col >= CON_COLS - 1) {
        con_newline();
    }
    con_lines[con_cur_row][con_cur_col++] = c;
    con_dirty = 1;
}

/* ── PS/2 keyboard ring buffer ─────────────────────────────────── */
#define PS2_BUF_SIZE 64
static uint8_t  ps2_buf[PS2_BUF_SIZE];
static uint32_t ps2_head = 0, ps2_tail = 0, ps2_count = 0;
static uint8_t  ps2_cmd_pending = 0;   /* waiting for param byte */
static uint8_t  ps2_scanning = 1;

static void ps2_enqueue(uint8_t code)
{
    if (ps2_count >= PS2_BUF_SIZE) return;
    ps2_buf[ps2_head] = code;
    ps2_head = (ps2_head + 1) % PS2_BUF_SIZE;
    ps2_count++;
}

static uint8_t ps2_dequeue(void)
{
    if (ps2_count == 0) return 0;
    uint8_t c = ps2_buf[ps2_tail];
    ps2_tail = (ps2_tail + 1) % PS2_BUF_SIZE;
    ps2_count--;
    return c;
}

/* ── COM1 serial emulation ─────────────────────────────────────── */
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
        con_putchar((char)val);
        host_serial_putchar((char)val);
        serial_char_count++;
        break;
    case 1: serial_ier = val; break;
    case 2: serial_fcr = val; break;
    case 3: serial_lcr = val; break;
    case 4: serial_mcr = val; break;
    case 7: serial_scr = val; break;
    default: break;
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
    case 0: return 0;                   /* RBR — no host→guest input via serial */
    case 1: return serial_ier;
    case 2: return 0x01;                /* IIR — no interrupt pending */
    case 3: return serial_lcr;
    case 4: return serial_mcr;
    case 5: return 0x60;                /* LSR — THRE + TEMT (TX ready) */
    case 6: return 0xB0;                /* MSR — CTS, DSR, DCD */
    case 7: return serial_scr;
    default: return 0;
    }
}

/* ── RTC (CMOS) emulation ──────────────────────────────────────── */
static uint8_t rtc_index = 0;
static uint8_t rtc_regs[128];

static void rtc_init(void)
{
    memset(rtc_regs, 0, sizeof(rtc_regs));
    rtc_regs[0x00] = 0x00;  rtc_regs[0x02] = 0x00;
    rtc_regs[0x04] = 0x12;  rtc_regs[0x06] = 0x01;
    rtc_regs[0x07] = 0x01;  rtc_regs[0x08] = 0x01;
    rtc_regs[0x09] = 0x25;  rtc_regs[0x0A] = 0x26;
    rtc_regs[0x0B] = 0x02;  rtc_regs[0x0C] = 0x00;
    rtc_regs[0x0D] = 0x80;  rtc_regs[0x32] = 0x20;
    /* Memory size fields expected by OVMF/SeaBIOS */
    rtc_regs[0x34] = 0x00;  rtc_regs[0x35] = 0x02; /* 512 ext mem pages */
}

static void rtc_handle_out(uint16_t port, uint8_t val)
{
    if (port == 0x70)      rtc_index = val & 0x7F;
    else if (port == 0x71 && rtc_index < 128) rtc_regs[rtc_index] = val;
}

static uint8_t rtc_handle_in(uint16_t port)
{
    if (port == 0x71 && rtc_index < 128) return rtc_regs[rtc_index];
    return 0;
}

/* ── PCI config space (all devices absent) ─────────────────────── */
static uint32_t pci_config_addr = 0;

static void pci_handle_out(uint16_t port, uint32_t val, uint8_t size)
{
    if (port == 0xCF8 && size == 4) pci_config_addr = val;
}

static uint32_t pci_handle_in(uint16_t port, uint8_t size)
{
    if (port == 0xCF8 && size == 4) return pci_config_addr;
    if (port >= 0xCFC && port <= 0xCFF) return 0xFFFFFFFF;
    return 0xFFFFFFFF;
    (void)size;
}

/* ── 8042 PS/2 controller emulation ────────────────────────────── */
static uint8_t i8042_cmd = 0;
static uint8_t i8042_status_extra = 0; /* extra status flags */

static void i8042_handle_out(uint16_t port, uint8_t val)
{
    if (port == 0x64) {
        /* Controller command */
        switch (val) {
        case 0xAA: /* Self-test → respond 0x55 */
            ps2_enqueue(0x55);
            break;
        case 0xAB: /* Interface test → respond 0x00 (pass) */
            ps2_enqueue(0x00);
            break;
        case 0xAD: /* Disable keyboard */
            ps2_scanning = 0;
            break;
        case 0xAE: /* Enable keyboard */
            ps2_scanning = 1;
            break;
        case 0xD1: /* Write output port — expect next byte on 0x60 */
        case 0xD4: /* Write to aux device */
            i8042_cmd = val;
            break;
        case 0x20: /* Read command byte */
            ps2_enqueue(0x47); /* xlat=1, SYS=1, INT=1, INT2=1 */
            break;
        case 0x60: /* Write command byte — expect next byte on 0x60 */
            i8042_cmd = val;
            break;
        case 0xA7: /* Disable aux */
        case 0xA8: /* Enable aux */
        case 0xA9: /* Test aux → pass */
            if (val == 0xA9) ps2_enqueue(0x00);
            break;
        default:
            break;
        }
    } else if (port == 0x60) {
        if (i8042_cmd == 0x60) {
            /* Write command byte — consume silently */
            i8042_cmd = 0;
        } else if (i8042_cmd == 0xD1) {
            /* Write output port (A20, reset) */
            i8042_cmd = 0;
        } else if (i8042_cmd == 0xD4) {
            /* Write to aux (mouse) — ACK */
            ps2_enqueue(0xFA);
            i8042_cmd = 0;
        } else {
            /* Keyboard command */
            switch (val) {
            case 0xFF: /* Reset */
                ps2_enqueue(0xFA);
                ps2_enqueue(0xAA);
                break;
            case 0xF5: /* Disable scanning */
                ps2_scanning = 0;
                ps2_enqueue(0xFA);
                break;
            case 0xF4: /* Enable scanning */
                ps2_scanning = 1;
                ps2_enqueue(0xFA);
                break;
            case 0xF2: /* Identify */
                ps2_enqueue(0xFA);
                ps2_enqueue(0xAB);
                ps2_enqueue(0x83);
                break;
            case 0xF0: /* Set scan code set — wait for param */
                ps2_cmd_pending = val;
                ps2_enqueue(0xFA);
                break;
            case 0xED: /* Set LEDs — wait for param */
                ps2_cmd_pending = val;
                ps2_enqueue(0xFA);
                break;
            case 0xF3: /* Set typematic rate — wait for param */
                ps2_cmd_pending = val;
                ps2_enqueue(0xFA);
                break;
            default:
                if (ps2_cmd_pending) {
                    /* Parameter for previous command */
                    ps2_cmd_pending = 0;
                    ps2_enqueue(0xFA);
                } else {
                    ps2_enqueue(0xFA);
                }
                break;
            }
        }
    }
}

static uint8_t i8042_handle_in(uint16_t port)
{
    if (port == 0x64) {
        /* Status register: bit0=OBF (data available), bit2=SYS */
        uint8_t status = 0x04; /* SYS flag — passed self-test */
        if (ps2_count > 0) status |= 0x01;  /* OBF */
        return status | i8042_status_extra;
    } else if (port == 0x60) {
        return ps2_dequeue();
    }
    return 0;
}

/* ── POST code tracking ────────────────────────────────────────── */
static uint8_t last_post_code = 0;
static uint8_t last_logged_post = 0xFF;

/* ── fw_cfg state ──────────────────────────────────────────────── */
static uint16_t fw_cfg_selector = 0;
static uint32_t fw_cfg_data_pos = 0;
/* ── kvm_run_t definition (matches kernel) ─────────────────────── */
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

/* ── Public: I/O handler ───────────────────────────────────────── */
void vm_handle_io(kvm_run_t *run)
{
    uint16_t port  = run->io.port;
    uint8_t  size  = run->io.size;
    uint8_t  is_in = run->io.direction;

    if (is_in) {
        uint32_t val = 0;

        if (port >= COM1_BASE && port <= COM1_BASE + 7) {
            val = serial_handle_in(port);
        } else if (port == 0x000 && size == 4) {
            /* Dummy timer to satisfy PM Timer or unknown delay loops */
            static uint32_t dummy_timer = 0;
            dummy_timer += 3579; /* simulate ~1ms tick per read if polling */
            val = dummy_timer & 0xFFFFFF;
        } else if (port == 0x71) {
            val = rtc_handle_in(port);
        } else if (port >= 0xCF8 && port <= 0xCFF) {
            val = pci_handle_in(port, size);
        } else if (port == 0x60 || port == 0x64) {
            val = i8042_handle_in(port);
        } else if (port == 0x511) {
            val = 0;
            for (int i = 0; i < size; i++) {
                uint8_t val8 = 0;
                if (fw_cfg_selector == 0x0000) { /* Signature */
                    const char *sig = "QEMU";
                    if (fw_cfg_data_pos < 4) val8 = sig[fw_cfg_data_pos++];
                } else if (fw_cfg_selector == 0x0001) { /* ID */
                    if (fw_cfg_data_pos == 0) val8 = 1;
                    else val8 = 0;
                    fw_cfg_data_pos++;
                } else if (fw_cfg_selector == 0x0005) { /* NB_CPUS */
                    if (fw_cfg_data_pos == 0) val8 = 1;
                    else val8 = 0;
                    fw_cfg_data_pos++;
                } else if (fw_cfg_selector == 0x000F) { /* MAX_CPUS */
                    if (fw_cfg_data_pos == 0) val8 = 1;
                    else val8 = 0;
                    fw_cfg_data_pos++;
                }
                val |= (val8 << (i * 8));
            }
        } else {
            switch (port) {
            case 0x61:  val = 0x20; break;
            case 0x20: case 0x21: case 0xA0: case 0xA1:
                val = 0x00; break;
            case 0x40: case 0x41: case 0x42: case 0x43:
                val = 0x00; break;
            case 0x92:  val = 0x02; break;
            case 0x80:  val = last_post_code; break;
            /* COM2-COM4 absent */
            case 0x2F8: case 0x2F9: case 0x2FA: case 0x2FB:
            case 0x2FC: case 0x2FD: case 0x2FE: case 0x2FF:
            case 0x3E8: case 0x3E9: case 0x3EA: case 0x3EB:
            case 0x3EC: case 0x3ED: case 0x3EE: case 0x3EF:
                val = 0xFF; break;
            default:
                val = 0xFF; break;
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
        } else if (port == 0x60 || port == 0x64) {
            i8042_handle_out(port, (uint8_t)val);
        } else if (port == 0x510) {
            fw_cfg_selector = (uint16_t)val;
            fw_cfg_data_pos = 0;
        } else if (port == 0x80) {
            last_post_code = (uint8_t)val;
            if (last_post_code != last_logged_post) {
                last_logged_post = last_post_code;
            }
        }
    }
}

/* ── Public: MMIO handler ──────────────────────────────────────── */
void vm_handle_mmio(kvm_run_t *run)
{
    if (!run->mmio.is_write) {
        memset(run->mmio.data, 0, run->mmio.len);

        /* LAPIC registers */
        if (run->mmio.phys_addr >= 0xFEE00000ULL &&
            run->mmio.phys_addr <  0xFEE01000ULL) {
            uint32_t offset = (uint32_t)(run->mmio.phys_addr & 0xFFF);
            uint32_t val = 0;
            switch (offset) {
            case 0x20:  val = 0; break;             /* LAPIC ID */
            case 0x30:  val = 0x00050014; break;    /* LAPIC version */
            case 0x80:  val = 0; break;             /* TPR */
            case 0xD0:  val = 0; break;             /* LDR */
            case 0xE0:  val = 0xFFFFFFFF; break;    /* DFR */
            case 0xF0:  val = 0xFF; break;          /* SVR */
            default:    val = 0; break;
            }
            memcpy(run->mmio.data, &val, 4);
        }
        /* IOAPIC */
        else if (run->mmio.phys_addr >= 0xFEC00000ULL &&
                 run->mmio.phys_addr <  0xFEC00100ULL) {
            uint32_t val = 0;
            uint32_t offset = (uint32_t)(run->mmio.phys_addr & 0xFF);
            if (offset == 0x00) val = 0;            /* IOREGSEL */
            else if (offset == 0x10) val = 0x00170020; /* version: 24 entries */
            memcpy(run->mmio.data, &val, 4);
        }
    }
    /* Writes to LAPIC/IOAPIC silently consumed */
}

/* ── Public: Console window ────────────────────────────────────── */
void vm_devices_set_window(uint32_t window_id)
{
    con_win_id = window_id;
}

void vm_devices_redraw(uint64_t exit_count)
{
    if (con_win_id == 0) return;

    /* Always update the title bar and POST/Exit display so we see it's alive */
    window_draw_text(con_win_id, 8, 4,
                     "OVMF Serial Console", 0xFF89B4FA, 13.0f);

    char post_str[64];
    snprintf(post_str, sizeof(post_str), "POST: 0x%02X | Exits: %llu      ", 
             last_post_code, (unsigned long long)exit_count);
    window_draw_text(con_win_id, 500, 4, post_str, 0xFF6C7086, 12.0f);

    if (!con_dirty) {
        draw_present();
        return;
    }
    con_dirty = 0;

    /* Clear window background for the console area */
    /* Note: window_clear clears the whole window, so we must redraw the title too.
       Wait, if we clear the whole window, we must do it before drawing the title! */
    window_clear(con_win_id);
    window_draw_text(con_win_id, 8, 4,
                     "OVMF Serial Console", 0xFF89B4FA, 13.0f);
    window_draw_text(con_win_id, 500, 4, post_str, 0xFF6C7086, 12.0f);

    /* Determine which lines to show */
    uint32_t visible = CON_VISIBLE;
    uint32_t start_y = 24;

    uint32_t total = (con_total < visible) ? con_total : visible;
    uint32_t first_row;
    if (con_total <= visible) {
        first_row = 0;
    } else {
        first_row = (con_cur_row + CON_ROWS - visible + 1) % CON_ROWS;
    }

    for (uint32_t i = 0; i < total; i++) {
        uint32_t row = (first_row + i) % CON_ROWS;
        if (con_lines[row][0] == '\0' && row != con_cur_row) continue;

        /* Null-terminate for safety */
        con_lines[row][CON_COLS - 1] = '\0';

        uint32_t y = start_y + i * 16;
        window_draw_text(con_win_id, 8, y,
                         con_lines[row], 0xFFCDD6F4, 13.0f);
    }

    draw_present();
}

/* ── Public: PS/2 keyboard ─────────────────────────────────────── */
void vm_devices_inject_scancode(uint8_t scancode)
{
    if (ps2_scanning) {
        ps2_enqueue(scancode);
    }
}

int vm_devices_has_ps2_data(void)
{
    return (int)ps2_count;
}

uint8_t vm_devices_get_post_code(void)
{
    return last_post_code;
}

/* ── Public: Init ──────────────────────────────────────────────── */
void vm_devices_init(void)
{
    rtc_init();
    serial_ier = serial_lcr = serial_mcr = 0;
    serial_dll = serial_dlh = serial_fcr = serial_scr = 0;
    pci_config_addr = 0;
    last_post_code = 0;
    last_logged_post = 0xFF;
    ps2_head = ps2_tail = ps2_count = 0;
    ps2_cmd_pending = 0;
    ps2_scanning = 1;
    i8042_cmd = 0;
    i8042_status_extra = 0;
    esc_state = 0;
    esc_len = 0;

    memset(con_lines, 0, sizeof(con_lines));
    con_cur_row = 0;
    con_cur_col = 0;
    con_total = 1;
    con_dirty = 1;
    con_win_id = 0;
}
