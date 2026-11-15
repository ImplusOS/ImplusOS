/*
 * IG9_Sim.c - host-side simulator for the IntelGen9 display driver module.
 *
 * QEMU has no Intel GPU, so this is the only place the driver's code paths
 * actually execute before real hardware. The driver sources are compiled
 * unmodified for the host with -DIG9_HOST_SIM, which routes their MMIO
 * accesses to ig9_sim_mmio_read32/write32 below. Behind those sits a model
 * of the parts of a Gen9 GPU the driver touches:
 *
 *   - PCI: host bridge + iGPU, GMCH_CTRL, BAR0 (16 MiB) and BAR2 aperture;
 *   - GGTT in the upper half of BAR0, aperture reads/writes through it;
 *   - forcewake request/ack (optionally needing the fallback kick);
 *   - RESET_CTL / GDRST.BLT engine reset;
 *   - execlists: RING_MODE.RUN_LIST_ENABLE, ELSP, the context image parsed
 *     as MI_LOAD_REGISTER_IMM, HEAD saved back, CSB events and pointers;
 *   - the legacy ring (RING_START/CTL/HEAD/TAIL);
 *   - a strict command decoder: MI_NOOP, MI_USER_INTERRUPT,
 *     MI_BATCH_BUFFER_START_GEN8 (GGTT only), MI_BATCH_BUFFER_END,
 *     MI_STORE_DWORD_IMM_GEN4, MI_FLUSH_DW (+post-sync), Gen8 XY_COLOR_BLT
 *     (7 dw) and XY_SRC_COPY_BLT (10 dw). Anything else, a wrong length, a
 *     non-present GGTT page or a command straddling the ring end hangs the
 *     engine, exactly what real silicon would make of it.
 *   - display: pipe A / plane 1 scanning out the GOP framebuffer, DDI
 *     transcoder, a frame counter ticking at 60 Hz of simulated time.
 *
 * It verifies logic and encodings against the model, not against silicon.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "Drivers/Module/DriverBinary.h"
#include "IG9_Regs.h"

/* Driver internals the tests inspect. */
typedef enum {
    IG9_SUBMIT_NONE = 0,
    IG9_SUBMIT_EXECLISTS,
    IG9_SUBMIT_LEGACY_RING,
} ig9_submit_mode_t;
ig9_submit_mode_t ig9_engine_mode(void);

/* ------------------------------------------------------------------ *
 * Configuration of one scenario
 * ------------------------------------------------------------------ */

typedef struct {
    bool intel_present;
    bool execlists_supported;
    bool engine_dead;           /* executes nothing at all */
    bool fw_needs_fallback;
    bool scratch_ggtt;          /* unused GGTT entries -> scratch page */
    bool plane_tiled;
    uint32_t hang_on_batch;     /* 1-based batch index that hangs (0 = never) */
    uint32_t hang_every_after;  /* after the first hang, hang every batch */
    bool fb_in_stolen_phys;     /* GOP hands the stolen physical address */
} sim_config_t;

static sim_config_t cfg;

/* ------------------------------------------------------------------ *
 * Simulated physical memory
 * ------------------------------------------------------------------ */

#define POOL_BYTES   (192u * 1024u * 1024u)
#define PHYS_BASE    0x100000000ull
#define BAR0_PHYS    0xF0000000ull
#define BAR0_SIZE    (16u * 1024u * 1024u)
#define GMADR_PHYS   0xC0000000ull
#define GMADR_SIZE   (256u * 1024u * 1024u)
#define GGTT_OFF     (BAR0_SIZE / 2u)
#define GGTT_ENTRIES (1u << 20)          /* GGMS=3: 8 MiB of PTEs */

#define FB_W 800u
#define FB_H 600u
#define FB_STRIDE 832u                    /* pixels; pitch 3328 = 52*64 */

static uint8_t *pool;
static uint64_t pool_used;
static uint64_t stolen_phys;              /* framebuffer backing */
static uint64_t scratch_phys;

static void *pool_alloc(uint64_t bytes, uint64_t align)
{
    pool_used = (pool_used + align - 1u) & ~(align - 1u);
    if (pool_used + bytes > POOL_BYTES) {
        fprintf(stderr, "sim: pool exhausted\n");
        exit(90);
    }
    void *p = pool + pool_used;
    pool_used += bytes;
    return p;
}

static uint64_t host_to_phys(const void *p)
{
    return PHYS_BASE + (uint64_t)((const uint8_t *)p - pool);
}

static uint8_t *phys_to_host(uint64_t phys)
{
    if (phys < PHYS_BASE || phys - PHYS_BASE >= POOL_BYTES) {
        return NULL;
    }
    return pool + (phys - PHYS_BASE);
}

/* ------------------------------------------------------------------ *
 * Device state
 * ------------------------------------------------------------------ */

static uint32_t regs[0x200000u / 4u];
static uint64_t *ggtt;
static uint64_t now_ns = 1000000000ull;

typedef struct {
    bool hung;
    uint32_t elsp[4];
    uint32_t elsp_count;
    uint32_t csb_wp;            /* 7 after reset */
    uint32_t csb_rp;
    uint32_t batches;
    uint32_t hangs;
    uint32_t elsp_submits;
    uint32_t legacy_submits;
    uint32_t blits;
    uint32_t resets;
} engine_state_t;

static engine_state_t eng;
static uint32_t pci_command = 0x0u;
static uint32_t mmio_accesses;

static uint32_t reg(uint32_t off) { return regs[off / 4u]; }
static void set_reg(uint32_t off, uint32_t v) { regs[off / 4u] = v; }

static bool is_masked(uint32_t off)
{
    return off == IG9_FORCEWAKE_GT || off == IG9_FORCEWAKE_RENDER ||
           off == IG9_RING_MODE(IG9_BCS_BASE) ||
           off == IG9_RING_MI_MODE(IG9_BCS_BASE) ||
           off == IG9_RING_RESET_CTL(IG9_BCS_BASE) ||
           off == IG9_RING_CONTEXT_CONTROL(IG9_BCS_BASE);
}

static void engine_reset_state(void)
{
    const uint32_t b = IG9_BCS_BASE;
    set_reg(IG9_RING_HEAD(b), 0);
    set_reg(IG9_RING_TAIL(b), 0);
    set_reg(IG9_RING_START(b), 0);
    set_reg(IG9_RING_CTL(b), 0);
    set_reg(IG9_RING_MODE(b), 0);
    set_reg(IG9_RING_MI_MODE(b), 0);
    eng.hung = false;
    eng.elsp_count = 0;
    eng.csb_wp = 7;
    eng.csb_rp = 7;
    eng.resets++;
}

/* GPU view of memory: every access goes through the GGTT. */
static uint32_t *gtt_ptr(uint32_t addr)
{
    uint32_t idx = addr >> 12;
    if (idx >= GGTT_ENTRIES || (addr & 3u) != 0u) {
        return NULL;
    }
    uint64_t pte = ggtt[idx];
    if ((pte & 1u) == 0u) {
        return NULL;
    }
    uint8_t *host = phys_to_host(pte & IG9_GGTT_PTE_ADDR_MASK);
    return host ? (uint32_t *)(void *)(host + (addr & 0xFFFu)) : NULL;
}

static bool fault(const char *why, uint32_t a, uint32_t b)
{
    printf("  [gpu] HANG: %s (0x%08X 0x%08X)\n", why, a, b);
    eng.hung = true;
    eng.hangs++;
    return false;
}

/* Independent of the driver's header on purpose: Gen8+ blit lengths
 * (64-bit addresses) per Mesa intel_blit.c / xf86-video-intel sna. */
#define SIM_XY_COLOR_BLT_LEN    7u
#define SIM_XY_SRC_COPY_BLT_LEN 10u

static bool blit(const uint32_t *c, bool copy)
{
    uint32_t len = copy ? SIM_XY_SRC_COPY_BLT_LEN : SIM_XY_COLOR_BLT_LEN;
    if ((c[0] & 0xFFu) != len - 2u) {
        return fault("blit length", c[0], len);
    }
    if ((c[0] & (IG9_BLT_WRITE_ALPHA | IG9_BLT_WRITE_RGB)) !=
        (IG9_BLT_WRITE_ALPHA | IG9_BLT_WRITE_RGB) ||
        (c[0] & (IG9_BLT_SRC_TILED | IG9_BLT_DST_TILED)) != 0u) {
        return fault("blit flags", c[0], 0);
    }
    if ((c[1] & (3u << 24)) != IG9_BLT_DEPTH_32 ||
        (c[1] & 0x00FF0000u) != (copy ? IG9_BLT_ROP_SRC_COPY : IG9_BLT_ROP_PAT_COPY)) {
        return fault("blit BR13", c[1], 0);
    }
    uint32_t pitch = c[1] & 0xFFFFu;
    uint32_t x1 = c[2] & 0xFFFFu, y1 = c[2] >> 16;
    uint32_t x2 = c[3] & 0xFFFFu, y2 = c[3] >> 16;
    if (x2 <= x1 || y2 <= y1 || c[5] != 0u) {
        return fault("blit rect/addr-hi", c[2], c[3]);
    }
    uint32_t dst = c[4];
    uint32_t src = 0, spitch = 0, sx = 0, sy = 0;
    if (copy) {
        sx = c[6] & 0xFFFFu;
        sy = c[6] >> 16;
        spitch = c[7] & 0xFFFFu;
        src = c[8];
        if (c[9] != 0u) {
            return fault("src addr-hi", c[9], 0);
        }
    }
    for (uint32_t y = y1; y < y2; ++y) {
        for (uint32_t x = x1; x < x2; ++x) {
            uint32_t *d = gtt_ptr(dst + y * pitch + x * 4u);
            if (d == NULL) {
                return fault("blit dst not mapped", dst, y * pitch + x * 4u);
            }
            if (copy) {
                uint32_t *s = gtt_ptr(src + (sy + y - y1) * spitch +
                                      (sx + x - x1) * 4u);
                if (s == NULL) {
                    return fault("blit src not mapped", src, 0);
                }
                *d = *s;
            } else {
                *d = c[6];
            }
        }
    }
    eng.blits++;
    return true;
}

static bool exec_batch(uint32_t addr)
{
    eng.batches++;
    if (cfg.hang_on_batch != 0u &&
        (eng.batches == cfg.hang_on_batch ||
         (cfg.hang_every_after && eng.batches > cfg.hang_on_batch))) {
        return fault("injected hang", eng.batches, 0);
    }
    for (uint32_t guard = 0; guard < 100000u; ++guard) {
        uint32_t *p = gtt_ptr(addr);
        if (p == NULL) {
            return fault("batch not mapped", addr, 0);
        }
        uint32_t dw = p[0];
        uint32_t client = dw >> 29;
        if (client == 2u) {
            uint32_t op = IG9_BLT_OPCODE(dw);
            uint32_t cmd[10];
            uint32_t len = (dw & 0xFFu) + 2u;
            if (len > 10u) {
                return fault("blit too long", dw, 0);
            }
            for (uint32_t i = 0; i < len; ++i) {
                uint32_t *q = gtt_ptr(addr + i * 4u);
                if (q == NULL) {
                    return fault("batch not mapped", addr, i);
                }
                cmd[i] = *q;
            }
            if (op == 0x53u) {
                if (!blit(cmd, true)) return false;
            } else if (op == 0x50u) {
                if (!blit(cmd, false)) return false;
            } else {
                return fault("unknown blit", dw, 0);
            }
            addr += len * 4u;
            continue;
        }
        if (client != 0u) {
            return fault("unknown client in batch", dw, addr);
        }
        switch (IG9_MI_OPCODE(dw)) {
        case 0x00u: addr += 4u; break;
        case 0x0Au: return true;
        case 0x20u:
            if ((dw & 0x3FFu) != 2u || (dw & IG9_MI_USE_GGTT) == 0u) {
                return fault("MI_STORE_DWORD_IMM form", dw, 0);
            } else {
                uint32_t *d = gtt_ptr(p[1]);
                if (d == NULL || p[2] != 0u) {
                    return fault("store target", p[1], p[2]);
                }
                *d = p[3];
            }
            addr += 16u;
            break;
        default:
            return fault("unknown MI in batch", dw, addr);
        }
    }
    return fault("runaway batch", addr, 0);
}

/* Execute ring commands from *head to tail. */
static bool exec_ring(uint32_t start, uint32_t size, uint32_t *head,
                      uint32_t tail)
{
    while (*head != tail) {
        uint32_t *p = gtt_ptr(start + *head);
        if (p == NULL) {
            return fault("ring not mapped", start, *head);
        }
        uint32_t dw = p[0];
        uint32_t len;
        if ((dw >> 29) != 0u) {
            return fault("non-MI command in ring", dw, *head);
        }
        switch (IG9_MI_OPCODE(dw)) {
        case 0x00u: case 0x02u: len = 1u; break;
        case 0x31u:
            len = (dw & 0xFFu) + 2u;
            if (len != 3u) return fault("BB_START length", dw, 0);
            if ((dw & IG9_MI_BATCH_PPGTT) != 0u) {
                return fault("PPGTT batch (expected GGTT)", dw, 0);
            }
            if (p[2] != 0u) return fault("BB_START addr-hi", p[2], 0);
            if (!exec_batch(p[1])) return false;
            break;
        case 0x26u:
            len = (dw & 0x3Fu) + 2u;
            if (len != 4u) return fault("MI_FLUSH_DW length", dw, 0);
            if ((dw & IG9_MI_FLUSH_DW_OP_STOREDW) != 0u) {
                if ((p[1] & IG9_MI_FLUSH_DW_USE_GTT) == 0u || (p[1] & 3u) != 0u) {
                    return fault("MI_FLUSH_DW address", p[1], 0);
                }
                uint32_t *d = gtt_ptr(p[1] & ~7u);
                if (d == NULL) return fault("flush post-sync target", p[1], 0);
                *d = p[3];
            }
            break;
        case 0x20u:
            len = 4u;
            if ((dw & 0x3FFu) != 2u || (dw & IG9_MI_USE_GGTT) == 0u) {
                return fault("ring MI_STORE_DWORD_IMM form", dw, 0);
            } else {
                uint32_t *d = gtt_ptr(p[1]);
                if (d == NULL) return fault("store target", p[1], 0);
                *d = p[3];
            }
            break;
        default:
            return fault("unknown MI in ring", dw, *head);
        }
        if (*head + len * 4u > size) {
            return fault("command straddles ring end", dw, *head);
        }
        *head = (*head + len * 4u) % size;
    }
    return true;
}

static void csb_push(uint32_t status)
{
    eng.csb_wp = (eng.csb_wp >= IG9_CSB_ENTRIES) ? 0u
                 : (eng.csb_wp + 1u) % IG9_CSB_ENTRIES;
    set_reg(IG9_RING_CSB_BUF_LO(IG9_BCS_BASE, eng.csb_wp), status);
    set_reg(IG9_RING_CSB_BUF_HI(IG9_BCS_BASE, eng.csb_wp), 1u);
}

static void execlists_submit(void)
{
    const uint32_t b = IG9_BCS_BASE;
    uint64_t desc = ((uint64_t)eng.elsp[2] << 32) | eng.elsp[3];
    if (eng.elsp[0] != 0u || eng.elsp[1] != 0u) {
        fault("element 1 not empty", eng.elsp[0], eng.elsp[1]);
        return;
    }
    if ((desc & 1u) == 0u || ((desc >> 3) & 3u) != 1u || (desc & (1u << 8)) == 0u) {
        fault("bad context descriptor", (uint32_t)desc, (uint32_t)(desc >> 32));
        return;
    }
    uint32_t lrca = (uint32_t)desc & 0xFFFFF000u;
    uint32_t *img = gtt_ptr(lrca + 4096u);
    if (img == NULL || gtt_ptr(lrca) == NULL) {
        fault("context image not mapped", lrca, 0);
        return;
    }
    /* Restore: walk the image as an LRI command stream. */
    uint32_t head = 0, tail = 0, start = 0, ctl = 0, lri = 0;
    uint64_t pdp0 = 0;
    for (uint32_t i = 0; i < 1024u;) {
        uint32_t dw = img[i];
        if (dw == 0u) { ++i; continue; }
        if (dw == IG9_MI_BATCH_BUFFER_END) break;
        if (IG9_MI_OPCODE(dw) != 0x22u) {
            fault("unexpected dword in context image", dw, i);
            return;
        }
        uint32_t pairs = ((dw & 0xFFu) + 1u) / 2u;
        ++lri;
        for (uint32_t k = 0; k < pairs; ++k) {
            uint32_t r = img[i + 1u + 2u * k];
            uint32_t v = img[i + 2u + 2u * k];
            if (r == IG9_RING_HEAD(b)) head = v;
            else if (r == IG9_RING_TAIL(b)) tail = v;
            else if (r == IG9_RING_START(b)) start = v;
            else if (r == IG9_RING_CTL(b)) ctl = v;
            else if (r == IG9_RING_PDP_LDW(b, 0)) pdp0 |= v;
            else if (r == IG9_RING_PDP_UDW(b, 0)) pdp0 |= (uint64_t)v << 32;
            else if (r < b || r > b + 0x400u) {
                fault("LRI to non-BCS register", r, v);
                return;
            }
        }
        i += 1u + 2u * pairs;
    }
    if (lri != 2u || (ctl & 1u) == 0u || pdp0 == 0u ||
        phys_to_host(pdp0) == NULL) {
        fault("context image incomplete", lri, (uint32_t)pdp0);
        return;
    }
    uint32_t size = (ctl & 0x001FF000u) + 4096u;
    eng.elsp_submits++;
    csb_push(IG9_CSB_STATUS_IDLE_ACTIVE);
    if (!exec_ring(start, size, &head, tail)) {
        return;     /* hung: no completion event */
    }
    img[IG9_CTX_RING_HEAD + 1u] = head;   /* context save */
    set_reg(IG9_RING_HEAD(b), head);
    csb_push(IG9_CSB_STATUS_COMPLETE | IG9_CSB_STATUS_ACTIVE_IDLE);
}

static void legacy_kick(void)
{
    const uint32_t b = IG9_BCS_BASE;
    if ((reg(IG9_RING_CTL(b)) & 1u) == 0u ||
        (reg(IG9_RING_MODE(b)) & IG9_GFX_RUN_LIST_ENABLE) != 0u) {
        return;
    }
    uint32_t size = (reg(IG9_RING_CTL(b)) & 0x001FF000u) + 4096u;
    uint32_t head = reg(IG9_RING_HEAD(b)) & IG9_RING_HEAD_ADDR_MASK;
    eng.legacy_submits++;
    if (exec_ring(reg(IG9_RING_START(b)), size, &head, reg(IG9_RING_TAIL(b)))) {
        set_reg(IG9_RING_HEAD(b), head);
    }
}

uint32_t ig9_sim_mmio_read32(uint32_t off);
void ig9_sim_mmio_write32(uint32_t off, uint32_t value);

uint32_t ig9_sim_mmio_read32(uint32_t off)
{
    mmio_accesses++;
    if (off >= GGTT_OFF) {
        uint32_t idx = (off - GGTT_OFF) / 8u;
        return (off & 4u) ? (uint32_t)(ggtt[idx] >> 32) : (uint32_t)ggtt[idx];
    }
    if (off == IG9_RING_CSB_PTR(IG9_BCS_BASE)) {
        return (eng.csb_rp << 8) | eng.csb_wp;
    }
    if (off == IG9_PIPE_FRMCOUNT(0)) {
        return (uint32_t)(now_ns / 16666667ull);
    }
    return reg(off);
}

void ig9_sim_mmio_write32(uint32_t off, uint32_t v)
{
    const uint32_t b = IG9_BCS_BASE;
    mmio_accesses++;
    if (off >= GGTT_OFF) {
        uint32_t idx = (off - GGTT_OFF) / 8u;
        if (off & 4u) ggtt[idx] = (ggtt[idx] & 0xFFFFFFFFull) | ((uint64_t)v << 32);
        else ggtt[idx] = (ggtt[idx] & ~0xFFFFFFFFull) | v;
        return;
    }
    if (off == IG9_RING_CSB_PTR(b)) {
        if ((v >> 16) & (IG9_CSB_PTR_MASK << 8)) eng.csb_rp = (v >> 8) & 7u;
        return;
    }
    if (is_masked(off)) {
        uint32_t mask = v >> 16;
        v = (reg(off) & ~mask) | (v & mask);
    }
    set_reg(off, v);

    if (off == IG9_FORCEWAKE_GT || off == IG9_FORCEWAKE_RENDER) {
        uint32_t ack = off == IG9_FORCEWAKE_GT ? IG9_FORCEWAKE_ACK_GT
                                               : IG9_FORCEWAKE_ACK_RENDER;
        uint32_t a = v & (IG9_FORCEWAKE_KERNEL | IG9_FORCEWAKE_KERNEL_FALLBACK);
        if (cfg.fw_needs_fallback && (a & IG9_FORCEWAKE_KERNEL_FALLBACK) == 0u &&
            (reg(ack) & IG9_FORCEWAKE_KERNEL_FALLBACK) == 0u) {
            /* the kernel bit is not acknowledged until a fallback kick */
            a &= ~IG9_FORCEWAKE_KERNEL;
            a |= reg(ack) & IG9_FORCEWAKE_KERNEL;
        }
        set_reg(ack, a | ((v & IG9_FORCEWAKE_KERNEL) ? (reg(ack) & IG9_FORCEWAKE_KERNEL) : 0));
        if ((v & IG9_FORCEWAKE_KERNEL_FALLBACK) != 0u) {
            set_reg(ack, reg(ack) | IG9_FORCEWAKE_KERNEL | IG9_FORCEWAKE_KERNEL_FALLBACK);
        } else if (cfg.fw_needs_fallback) {
            set_reg(ack, reg(ack) & ~IG9_FORCEWAKE_KERNEL_FALLBACK);
        }
        if ((v & IG9_FORCEWAKE_KERNEL) == 0u) {
            set_reg(ack, reg(ack) & ~IG9_FORCEWAKE_KERNEL);
        }
    } else if (off == IG9_RING_RESET_CTL(b)) {
        uint32_t r = reg(off) & ~IG9_RESET_CTL_READY_TO_RESET;
        if (r & IG9_RESET_CTL_REQUEST_RESET) r |= IG9_RESET_CTL_READY_TO_RESET;
        set_reg(off, r);
    } else if (off == IG9_GEN6_GDRST) {
        if (v & IG9_GDRST_RENDER) {
            printf("  [gpu] ERROR: render engine reset requested\n");
            exit(80);
        }
        if (v & IG9_GDRST_BLT) {
            if ((reg(IG9_FORCEWAKE_ACK_GT) & 1u) == 0u) {
                printf("  [gpu] ERROR: reset without forcewake\n");
                exit(81);
            }
            engine_reset_state();
        }
        set_reg(off, 0);
    } else if (off == IG9_RING_ELSP(b)) {
        if (cfg.engine_dead || !cfg.execlists_supported ||
            (reg(IG9_RING_MODE(b)) & IG9_GFX_RUN_LIST_ENABLE) == 0u) {
            return;
        }
        eng.elsp[eng.elsp_count++] = v;
        if (eng.elsp_count == 4u) {
            eng.elsp_count = 0u;
            if (!eng.hung) execlists_submit();
        }
    } else if (off == IG9_RING_TAIL(b)) {
        if (!cfg.engine_dead && !eng.hung) legacy_kick();
    } else if (off == IG9_RING_MODE(b)) {
        if (!cfg.execlists_supported) {
            set_reg(off, reg(off) & ~IG9_GFX_RUN_LIST_ENABLE);
        }
    } else if (off >= 0x22000u && off < 0x23000u &&
               (reg(IG9_FORCEWAKE_ACK_GT) & 1u) == 0u) {
        printf("  [gpu] ERROR: BCS register 0x%X written without forcewake\n", off);
        exit(82);
    }
}

/* ------------------------------------------------------------------ *
 * driver_binary_t
 * ------------------------------------------------------------------ */

static char logbuf[1 << 20];
static size_t loglen;
static bool verbose;

static void s_write_string(const char *s)
{
    size_t n = strlen(s);
    if (loglen + n < sizeof(logbuf)) {
        memcpy(logbuf + loglen, s, n);
        loglen += n;
        logbuf[loglen] = '\0';
    }
    if (verbose) fputs(s, stdout);
}

static void s_write_u32(uint32_t v)
{
    char b[16];
    snprintf(b, sizeof(b), "%08X", v);
    s_write_string(b);
}

static void s_msleep(uint32_t ms) { now_ns += (uint64_t)ms * 1000000u; }
static uint64_t s_mono(void) { now_ns += 1000u; return now_ns; }
static uint32_t s_hz(void) { return 1000u; }
static uint64_t s_ticks(void) { return now_ns / 1000000u; }
static void *s_malloc(uint64_t n) { return pool_alloc(n, 16u); }
static void s_free(void *p) { (void)p; }
static void *s_dma_alloc(size_t n, uint64_t *phys)
{
    void *p = pool_alloc(n, 4096u);
    *phys = host_to_phys(p);
    return p;
}
static void *s_dma_alloc_ex(size_t n, size_t align, uint64_t max, uint64_t *phys)
{
    (void)max;
    void *p = pool_alloc(n, align < 4096u ? 4096u : align);
    *phys = host_to_phys(p);
    return p;
}
static void s_dma_free(void *p, size_t n) { (void)p; (void)n; }
static uint64_t s_v2p(void *v) { return host_to_phys(v); }

static void *s_map(uint64_t phys, size_t size)
{
    (void)size;
    if (phys == BAR0_PHYS) {
        return (void *)(uintptr_t)0x1000u;   /* never dereferenced (hooks) */
    }
    if (phys >= GMADR_PHYS && phys < GMADR_PHYS + GMADR_SIZE) {
        /* CPU aperture access goes through the GGTT; the GOP buffer is
         * contiguous in the model, so translating the first page suffices. */
        uint32_t off = (uint32_t)(phys - GMADR_PHYS);
        uint64_t pte = ggtt[off >> 12];
        return phys_to_host((pte & IG9_GGTT_PTE_ADDR_MASK) + (off & 0xFFFu));
    }
    return phys_to_host(phys);
}

#define GPU_BDF 2u

static uint32_t s_pci_count(void) { return cfg.intel_present ? 2u : 2u; }
static bool s_pci_get(uint32_t i, driver_pci_device_t *d)
{
    memset(d, 0, sizeof(*d));
    if (i == 0u) {
        d->vendor_id = 0x8086u; d->device_id = 0x3E0Fu;
        d->class_code = 0x06u; d->subclass = 0x00u;
        return true;
    }
    if (i == 1u) {
        d->device = GPU_BDF;
        d->vendor_id = cfg.intel_present ? 0x8086u : 0x1234u;
        d->device_id = cfg.intel_present ? 0x3E92u : 0x1111u;
        d->class_code = 0x03u;
        return true;
    }
    return false;
}
static uint32_t s_pci_read(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off)
{
    (void)bus; (void)fn;
    if (dev == GPU_BDF && off == 0x04u) return pci_command;
    if (off == IG9_PCI_GMCH_CTRL) return 3u << IG9_GMCH_GGMS_SHIFT;
    return 0u;
}
static void s_pci_write(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint32_t v)
{
    (void)bus; (void)fn;
    if (dev == GPU_BDF && off == 0x04u) pci_command = v;
}
static bool s_pci_bar(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t idx,
                      driver_pci_bar_t *out)
{
    (void)bus; (void)fn;
    memset(out, 0, sizeof(*out));
    if (dev != GPU_BDF) return false;
    if (idx == 0u) { out->address = BAR0_PHYS; out->size = BAR0_SIZE; out->is_64bit = 1; return true; }
    if (idx == 2u) { out->address = GMADR_PHYS; out->size = GMADR_SIZE; out->is_64bit = 1; out->prefetchable = 1; return true; }
    return false;
}
static int32_t s_pci_cap(uint8_t b, uint8_t d, uint8_t f, uint8_t id)
{
    (void)b; (void)d; (void)f; (void)id;
    return -1;
}

static driver_binary_t api;

static void build_api(void)
{
    memset(&api, 0, sizeof(api));
    api.version_major = 2; api.version_minor = 3;
    api.timer.msleep = s_msleep; api.timer.hz = s_hz; api.timer.ticks = s_ticks;
    api.timer.monotonic_ns = s_mono;
    api.mem.dma_alloc_ex = s_dma_alloc_ex;
    api.hw.map_mmio_range = s_map;
    api.hw.map_mmio_wc_range = s_map;
    api.pci.get_device_count = s_pci_count;
    api.pci.get_device = s_pci_get;
    api.pci.read_config = s_pci_read;
    api.pci.write_config = s_pci_write;
    api.pci.get_bar = s_pci_bar;
    api.pci.find_capability = s_pci_cap;
    api.timer_msleep = s_msleep; api.timer_hz = s_hz; api.timer_ticks = s_ticks;
    api.malloc = s_malloc; api.free = s_free;
    api.dma_alloc = s_dma_alloc; api.dma_free = s_dma_free;
    api.virt_to_phys = s_v2p;
    api.memset = memset; api.memcpy = memcpy;
    api.serial_write_string = s_write_string;
    api.serial_write_uint32 = s_write_u32;
}

/* ------------------------------------------------------------------ *
 * World setup
 * ------------------------------------------------------------------ */

static uint32_t fb_pitch(void) { return FB_STRIDE * 4u; }
static uint32_t *scanout(void) { return (uint32_t *)(void *)phys_to_host(stolen_phys); }

static void world_init(void)
{
    pool = aligned_alloc(4096, POOL_BYTES);
    ggtt = calloc(GGTT_ENTRIES, sizeof(uint64_t));
    if (pool == NULL || ggtt == NULL) exit(91);
    memset(pool, 0, POOL_BYTES);

    uint64_t fb_bytes = (uint64_t)fb_pitch() * FB_H;
    uint8_t *stolen = pool_alloc(fb_bytes + 4096u, 4096u);
    stolen_phys = host_to_phys(stolen);
    scratch_phys = host_to_phys(pool_alloc(4096u, 4096u));

    if (cfg.scratch_ggtt) {
        for (uint32_t i = 0; i < GGTT_ENTRIES; ++i) ggtt[i] = scratch_phys | 1u;
    }
    /* GOP framebuffer pinned at GGTT 0x200000 (aperture offset 2 MiB). */
    for (uint32_t i = 0; i < (fb_bytes + 4095u) / 4096u; ++i) {
        ggtt[0x200u + i] = (stolen_phys + (uint64_t)i * 4096u) | 1u;
    }
    set_reg(IG9_PIPECONF(0), IG9_PIPECONF_ENABLE);
    set_reg(IG9_PIPESRC(0), ((FB_W - 1u) << 16) | (FB_H - 1u));
    set_reg(IG9_PLANE_CTL(0, 1), IG9_PLANE_CTL_ENABLE | IG9_PLANE_CTL_FORMAT_XRGB8888 |
                                 (cfg.plane_tiled ? IG9_PLANE_CTL_TILED_X : 0u));
    set_reg(IG9_PLANE_SURF(0, 1), 0x200000u);
    set_reg(IG9_PLANE_STRIDE(0, 1), fb_pitch() / 64u);
    set_reg(IG9_TRANS_DDI_FUNC_CTL(0), IG9_TRANS_DDI_ENABLE | (1u << IG9_TRANS_DDI_PORT_SHIFT) |
                                       (IG9_TRANS_DDI_MODE_HDMI << IG9_TRANS_DDI_MODE_SHIFT));
    engine_reset_state();
    eng.resets = 0;
    build_api();
}

const driver_module_descriptor_t *driver_module_init(const driver_binary_t *api);

static const driver_display_t *disp;

static bool bring_up(void)
{
    world_init();
    const driver_module_descriptor_t *d = driver_module_init(&api);
    if (d == NULL) return false;
    disp = (const driver_display_t *)d->driver_api;
    driver_boot_framebuffer_t fb = {
        .addr = (void *)(uintptr_t)(cfg.fb_in_stolen_phys ? stolen_phys
                                                          : GMADR_PHYS + 0x200000u),
        .size_bytes = fb_pitch() * FB_H,
        .width = FB_W, .height = FB_H,
        .pixels_per_scan_line = FB_STRIDE, .bytes_per_pixel = 4,
    };
    /* Same order as Kernel/Source/Drivers/Module/DriverSelect.c, which
     * adopts a driver whose is_ready() is true before probe() runs. */
    if (!disp->set_framebuffer(&fb)) return false;
    if (disp->is_ready()) {
        printf("  is_ready() true before probe/init\n");
        return false;
    }
    if (!disp->probe()) return false;
    return disp->init() && disp->is_ready();
}

/* Draw a pattern into the back buffer, present rects, verify scanout. */
static uint32_t frame_no;

static bool present_and_verify(const display_rect_t *rects, uint32_t n)
{
    uint32_t *back = disp->get_framebuffer();
    uint32_t *out = scanout();
    ++frame_no;
    for (uint32_t y = 0; y < FB_H; ++y) {
        for (uint32_t x = 0; x < FB_W; ++x) {
            back[y * FB_STRIDE + x] = (frame_no << 24) ^ (y << 12) ^ x;
        }
    }
    static uint32_t before[FB_STRIDE * FB_H];
    memcpy(before, out, sizeof(before));
    disp->present_rects(rects, n);
    for (uint32_t y = 0; y < FB_H; ++y) {
        for (uint32_t x = 0; x < FB_W; ++x) {
            bool in = false;
            for (uint32_t i = 0; i < n && !in; ++i) {
                int64_t rx = rects[i].x, ry = rects[i].y;
                in = (int64_t)x >= rx && (int64_t)x < rx + rects[i].w &&
                     (int64_t)y >= ry && (int64_t)y < ry + rects[i].h;
            }
            uint32_t want = in ? back[y * FB_STRIDE + x] : before[y * FB_STRIDE + x];
            if (out[y * FB_STRIDE + x] != want) {
                printf("  scanout mismatch at (%u,%u): 0x%08X want 0x%08X\n",
                       x, y, out[y * FB_STRIDE + x], want);
                return false;
            }
        }
    }
    return true;
}

#define CHECK(cond) do { if (!(cond)) { printf("  CHECK failed: %s (line %d)\n", #cond, __LINE__); return 1; } } while (0)

static bool log_has(const char *s) { return strstr(logbuf, s) != NULL; }

static const display_rect_t r_basic[] = {
    { 10, 20, 100, 50 }, { -5, -5, 30, 30 }, { 700, 550, 200, 200 }, { 400, 0, 1, 600 },
};

/* ------------------------------------------------------------------ *
 * Scenarios
 * ------------------------------------------------------------------ */

static int t_execlists(void)
{
    CHECK(bring_up());
    CHECK(ig9_engine_mode() == IG9_SUBMIT_EXECLISTS);
    CHECK(log_has("execlists submission verified"));
    CHECK(log_has("blitter present enabled (execlists)"));
    uint32_t blits = eng.blits;
    CHECK(present_and_verify(r_basic, 4));
    CHECK(eng.blits == blits + 4u);
    CHECK(eng.legacy_submits == 0u && eng.hangs == 0u);

    display_monitor_info_t mi;
    CHECK(disp->get_monitor_info(0, &mi));
    CHECK(mi.output_type == DISPLAY_OUTPUT_HDMI);
    CHECK(strcmp(mi.output_name, "HDMI-B") == 0);
    CHECK(mi.refresh_millihz == 60000u);
    CHECK((mi.flags & DISPLAY_MONITOR_FLAG_SYNTHETIC_MODE) == 0u);
    CHECK(mi.width == FB_W && mi.height == FB_H);
    display_mode_info_t mode;
    CHECK(disp->get_mode_info(0, 0, &mode));
    CHECK(mode.stride == FB_STRIDE && mode.refresh_millihz == 60000u);
    return 0;
}

static int t_full_present(void)
{
    CHECK(bring_up());
    CHECK(ig9_engine_mode() == IG9_SUBMIT_EXECLISTS);
    display_rect_t full = { 0, 0, FB_W, FB_H };
    CHECK(present_and_verify(&full, 1));
    disp->present();      /* NULL rects path */
    return 0;
}

static int t_many_rects_and_ring_wrap(void)
{
    CHECK(bring_up());
    static display_rect_t rs[500];
    for (uint32_t i = 0; i < 500u; ++i) {
        rs[i].x = (int32_t)((i * 37u) % (FB_W - 8u));
        rs[i].y = (int32_t)((i * 53u) % (FB_H - 8u));
        rs[i].w = 1u + i % 7u;
        rs[i].h = 1u + i % 5u;
    }
    CHECK(present_and_verify(rs, 500));
    /* Each request is 10 dwords (40 bytes): 2000 presents wrap the
     * 32 KiB ring a couple of times. */
    display_rect_t r = { 1, 1, 2, 2 };
    for (uint32_t i = 0; i < 2000u; ++i) {
        disp->present_rects(&r, 1);
    }
    CHECK(eng.hangs == 0u);
    CHECK(present_and_verify(r_basic, 4));
    return 0;
}

static int t_legacy_fallback(void)
{
    cfg.execlists_supported = false;
    CHECK(bring_up());
    CHECK(ig9_engine_mode() == IG9_SUBMIT_LEGACY_RING);
    CHECK(log_has("execlists enable failed"));
    CHECK(present_and_verify(r_basic, 4));
    CHECK(eng.legacy_submits > 0u);
    return 0;
}

static int t_dead_engine(void)
{
    cfg.engine_dead = true;
    CHECK(bring_up());
    CHECK(ig9_engine_mode() == IG9_SUBMIT_NONE);
    CHECK(log_has("CPU present path only"));
    CHECK(present_and_verify(r_basic, 4));      /* CPU copy through aperture */
    return 0;
}

static int t_hang_recovery(void)
{
    /* Self-tests run 4 batches (store, fill, copy, copy): hang the first
     * present's batch. */
    cfg.hang_on_batch = 5u;
    CHECK(bring_up());
    CHECK(ig9_engine_mode() == IG9_SUBMIT_EXECLISTS);
    uint32_t resets = eng.resets;
    CHECK(present_and_verify(r_basic, 4));      /* hung -> CPU fallback */
    CHECK(eng.resets == resets + 1u);
    CHECK(ig9_engine_mode() == IG9_SUBMIT_EXECLISTS);
    uint32_t blits = eng.blits;
    CHECK(present_and_verify(r_basic, 4));      /* GPU again */
    CHECK(eng.blits == blits + 4u);
    return 0;
}

static int t_hang_disable(void)
{
    cfg.hang_on_batch = 5u;
    cfg.hang_every_after = 1u;
    CHECK(bring_up());
    for (uint32_t i = 0; i < 5u; ++i) {
        CHECK(present_and_verify(r_basic, 4));
    }
    CHECK(ig9_engine_mode() == IG9_SUBMIT_NONE);
    CHECK(log_has("disabled for this boot"));
    return 0;
}

static int t_scratch_ggtt(void)
{
    cfg.scratch_ggtt = true;
    CHECK(bring_up());
    CHECK(log_has("unused entries point at scratch pte"));
    CHECK(ig9_engine_mode() == IG9_SUBMIT_EXECLISTS);
    CHECK(present_and_verify(r_basic, 4));
    return 0;
}

static int t_tiled_plane(void)
{
    cfg.plane_tiled = true;
    CHECK(bring_up());
    CHECK(log_has("plane is tiled"));
    CHECK(ig9_engine_mode() == IG9_SUBMIT_NONE);
    CHECK(present_and_verify(r_basic, 4));
    return 0;
}

static int t_fw_fallback(void)
{
    cfg.fw_needs_fallback = true;
    CHECK(bring_up());
    CHECK(ig9_engine_mode() == IG9_SUBMIT_EXECLISTS);
    CHECK(present_and_verify(r_basic, 4));
    return 0;
}

static int t_stolen_phys(void)
{
    cfg.fb_in_stolen_phys = true;
    CHECK(bring_up());
    CHECK(log_has("framebuffer found in GGTT"));
    CHECK(ig9_engine_mode() == IG9_SUBMIT_EXECLISTS);
    CHECK(present_and_verify(r_basic, 4));
    return 0;
}

static int t_not_intel(void)
{
    cfg.intel_present = false;
    world_init();
    const driver_module_descriptor_t *d = driver_module_init(&api);
    CHECK(d != NULL);
    disp = (const driver_display_t *)d->driver_api;
    driver_boot_framebuffer_t fb = {
        .addr = (void *)(uintptr_t)(GMADR_PHYS + 0x200000u),
        .size_bytes = fb_pitch() * FB_H, .width = FB_W, .height = FB_H,
        .pixels_per_scan_line = FB_STRIDE, .bytes_per_pixel = 4,
    };
    (void)disp->set_framebuffer(&fb);
    CHECK(!disp->is_ready());
    CHECK(!disp->probe());
    CHECK(!disp->init());
    CHECK(!disp->is_ready());
    CHECK(mmio_accesses == 0u);
    CHECK(log_has("probe: no supported GPU"));
    return 0;
}

typedef struct {
    const char *name;
    int (*fn)(void);
} scenario_t;

static const scenario_t scenarios[] = {
    { "execlists bring-up + present + monitor info", t_execlists },
    { "full-screen present",                          t_full_present },
    { "500 rects + ring wrap (2000 presents)",        t_many_rects_and_ring_wrap },
    { "legacy ring fallback",                         t_legacy_fallback },
    { "dead engine -> CPU path",                      t_dead_engine },
    { "hang -> reset -> recover",                     t_hang_recovery },
    { "repeated hangs -> engine disabled",            t_hang_disable },
    { "scratch-filled GGTT",                          t_scratch_ggtt },
    { "X-tiled plane -> CPU path",                    t_tiled_plane },
    { "forcewake needs fallback kick",                t_fw_fallback },
    { "GOP gives stolen physical address",            t_stolen_phys },
    { "non-Intel machine: no MMIO",                   t_not_intel },
};

int main(int argc, char **argv)
{
    verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    int failed = 0;
    size_t count = sizeof(scenarios) / sizeof(scenarios[0]);
    for (size_t i = 0; i < count; ++i) {
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            cfg = (sim_config_t){ .intel_present = true, .execlists_supported = true };
            int rc = scenarios[i].fn();
            if (rc != 0 && !verbose) {
                printf("  --- driver log ---\n%s", logbuf);
            }
            fflush(stdout);
            _exit(rc);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        printf("%s  %s\n", ok ? "PASS" : "FAIL", scenarios[i].name);
        failed += ok ? 0 : 1;
    }
    printf("%d/%zu scenarios passed\n", (int)(count - (size_t)failed), count);
    return failed == 0 ? 0 : 1;
}
