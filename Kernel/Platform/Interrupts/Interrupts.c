#include "Interrupts.h"

#include "../../IO/IO_Main.h"
#include "../APIC/LAPIC.h"
#include "../APIC/IOAPIC.h"

static int g_use_lapic = 0;

void platform_interrupts_init_legacy_pic(void)
{
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

int platform_interrupts_configure(const acpi_info_t *info)
{
    if (info == NULL) {
        platform_interrupts_init_legacy_pic();
        return -1;
    }

    platform_interrupts_init_legacy_pic();

    if (info->lapic_base != 0) {
        if (lapic_init(info->lapic_base) == 0) {
            g_use_lapic = 1;
        }
    }

    if (info->ioapic_base != 0) {
        ioapic_init(info->ioapic_base, info->ioapic_gsi_base);
        ioapic_mask_all();
    }

    return 0;
}

void platform_interrupts_eoi(uint16_t vector)
{
    if (g_use_lapic) {
        lapic_eoi();
    } else {
        outb(0x20, 0x20);
        if (vector >= 40 && vector < 48) {
            outb(0xA0, 0x20);
        }
    }
}

int platform_interrupts_using_lapic(void)
{
    return g_use_lapic;
}

void platform_interrupts_route_pit(void)
{
    if (g_use_lapic) {
        const acpi_info_t *acpi = acpi_get_info();
        if (acpi != NULL) {
            ioapic_route_irq((uint8_t)acpi->pit_gsi,
                             VECTOR_TIMER,
                             0,
                             acpi->pit_level_trigger != 0,
                             acpi->pit_active_low != 0);
        }
    }
}

void platform_interrupts_mask_pit(void)
{
    if (g_use_lapic) {
        const acpi_info_t *acpi = acpi_get_info();
        if (acpi != NULL) {
            ioapic_mask_irq(acpi->pit_gsi);
        }
    }
}
