#include "GIC.h"
#include "interfaces/interrupt_ops.h"
#include "Platform/acpi/ACPI.h"

#define GICD_CTLR       0x0000
#define GICD_IGROUPR    0x0080
#define GICD_ISENABLER  0x0100
#define GICD_ICENABLER  0x0180
#define GICD_IPRIORITYR 0x0400
#define GICD_ITARGETSR  0x0800
#define GICC_CTLR       0x0000
#define GICC_PMR        0x0004
#define GICC_IAR        0x000C
#define GICC_EOIR       0x0010

static volatile uint32_t *g_gicd;
static volatile uint32_t *g_gicc;
static int g_gic_ready;

static inline uint32_t mmio_read32(volatile uint32_t *base, uint64_t off)
{
    return base[off / 4];
}

static inline void mmio_write32(volatile uint32_t *base, uint64_t off, uint32_t value)
{
    base[off / 4] = value;
}

int arm64_gic_init(uint64_t gicd_base, uint64_t gicr_base, uint64_t gicc_base)
{
    (void)gicr_base;
    if (gicd_base == 0) return -1;
    g_gicd = (volatile uint32_t *)(uintptr_t)gicd_base;
    g_gicc = (volatile uint32_t *)(uintptr_t)gicc_base;

    mmio_write32(g_gicd, GICD_CTLR, 0);
    for (uint32_t i = 0; i < 32; ++i) {
        mmio_write32(g_gicd, GICD_IGROUPR + i * 4, 0xFFFFFFFFu);
        mmio_write32(g_gicd, GICD_ICENABLER + i * 4, 0xFFFFFFFFu);
    }
    for (uint32_t i = 0; i < 256; ++i) {
        mmio_write32(g_gicd, GICD_IPRIORITYR + i * 4, 0xA0A0A0A0u);
    }
    mmio_write32(g_gicd, GICD_CTLR, 3);

    if (g_gicc) {
        mmio_write32(g_gicc, GICC_PMR, 0xFFu);
        mmio_write32(g_gicc, GICC_CTLR, 3);
    }

    g_gic_ready = 1;
    return 0;
}

void arm64_gic_eoi(uint32_t irq)
{
    if (g_gicc) mmio_write32(g_gicc, GICC_EOIR, irq);
}

uint32_t arm64_gic_read_iar(void)
{
    if (g_gicc) return mmio_read32(g_gicc, GICC_IAR);
    return 0x3FFu;
}

int arm64_gic_route_irq(uint32_t irq, uint32_t vector)
{
    (void)vector;
    if (!g_gic_ready || irq >= 1020) return -1;
    if (irq < 32 && g_gicc == 0) return 0;
    mmio_write32(g_gicd, GICD_ITARGETSR + irq, 0x01u);
    arm64_gic_unmask_irq(irq);
    return 0;
}

void arm64_gic_mask_irq(uint32_t irq)
{
    if (!g_gic_ready || irq >= 1020) return;
    mmio_write32(g_gicd, GICD_ICENABLER + ((irq / 32) * 4), 1u << (irq % 32));
}

void arm64_gic_unmask_irq(uint32_t irq)
{
    if (!g_gic_ready || irq >= 1020) return;
    mmio_write32(g_gicd, GICD_ISENABLER + ((irq / 32) * 4), 1u << (irq % 32));
}

static int arm64_interrupt_configure(const void *firmware_info)
{
    (void)firmware_info;
    return arm64_gic_init(0x08000000ULL, 0, 0x08010000ULL);
}

static int arm64_using_local_timer(void)
{
    return 1;
}

static const interrupt_ops_t g_arm64_interrupt_ops = {
    .configure = arm64_interrupt_configure,
    .eoi = arm64_gic_eoi,
    .route_irq = arm64_gic_route_irq,
    .mask_irq = arm64_gic_mask_irq,
    .unmask_irq = arm64_gic_unmask_irq,
    .using_local_timer = arm64_using_local_timer,
};

const interrupt_ops_t *interrupt_ops_get(void)
{
    return &g_arm64_interrupt_ops;
}

void platform_interrupts_init_legacy_pic(void)
{
}

int platform_interrupts_configure(const acpi_info_t *info)
{
    (void)info;
    return arm64_interrupt_configure(info);
}

void platform_interrupts_eoi(uint16_t vector)
{
    arm64_gic_eoi(vector);
}

int platform_interrupts_using_lapic(void)
{
    return 0;
}

void platform_interrupts_route_pit(void)
{
}

void platform_interrupts_mask_pit(void)
{
}

int lapic_init(uint64_t phys_base) { (void)phys_base; return -1; }
int lapic_is_present(void) { return 0; }
void lapic_eoi(void) {}
uint32_t lapic_get_id(void) { return 0; }
uint32_t lapic_timer_current(void) { return 0; }
int lapic_timer_start(uint8_t vector, uint32_t initial_count, int periodic, uint32_t divide)
{
    (void)vector; (void)initial_count; (void)periodic; (void)divide;
    return -1;
}
void lapic_timer_stop(void) {}
void lapic_send_ipi(uint8_t apic_id, uint32_t icr_low) { (void)apic_id; (void)icr_low; }
