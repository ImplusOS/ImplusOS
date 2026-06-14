#include "Drivers/Module/DriverBinary.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AC97_PERIOD_BYTES 2048u
#define AC97_PERIOD_COUNT 4u
#define AC97_TIMEOUT_MS   2000u

typedef struct __attribute__((packed)) {
    uint32_t address;
    uint16_t samples;
    uint16_t control;
} ac97_bdl_entry_t;

static const driver_binary_t *g_api;
static uint16_t g_nam;
static uint16_t g_nabm;
static ac97_bdl_entry_t *g_bdl;
static uint64_t g_bdl_phys;
static uint8_t *g_periods;
static uint64_t g_periods_phys;
static bool g_ready;
static bool g_open;

static bool ac97_find(void)
{
#if !defined(PLATFORM_X86_64)
    return false;
#else
    uint32_t count = g_api->pci.get_device_count();
    for (uint32_t i = 0u; i < count; ++i) {
        driver_pci_device_t device;
        driver_pci_bar_t nam;
        driver_pci_bar_t nabm;
        if (!g_api->pci.get_device(i, &device) ||
            device.class_code != 0x04u || device.subclass != 0x01u ||
            !g_api->pci.get_bar(device.bus, device.device,
                                device.function, 0u, &nam) ||
            !g_api->pci.get_bar(device.bus, device.device,
                                device.function, 1u, &nabm) ||
            !nam.is_io || !nabm.is_io ||
            nam.address > UINT16_MAX || nabm.address > UINT16_MAX) {
            continue;
        }
        g_nam = (uint16_t)nam.address;
        g_nabm = (uint16_t)nabm.address;
        g_api->pci.enable_bus_master(device.bus, device.device,
                                     device.function);
        return true;
    }
    return false;
#endif
}

static bool ac97_init(void)
{
    if (g_ready) {
        return true;
    }
    if (!ac97_find()) {
        return false;
    }
    g_bdl = g_api->mem.dma_alloc_ex(
        sizeof(ac97_bdl_entry_t) * 32u, 16u, UINT32_MAX, &g_bdl_phys);
    g_periods = g_api->mem.dma_alloc_ex(
        AC97_PERIOD_BYTES * AC97_PERIOD_COUNT, 16u, UINT32_MAX,
        &g_periods_phys);
    if (g_bdl == NULL || g_periods == NULL) {
        return false;
    }
    g_api->hal.io_out32((uint16_t)(g_nabm + 0x2Cu), 0x02u);
    g_api->timer.msleep(20u);
    g_api->hal.io_out16((uint16_t)(g_nam + 0x00u), 0x0000u);
    g_api->hal.io_out16((uint16_t)(g_nam + 0x02u), 0x0000u);
    g_api->hal.io_out16((uint16_t)(g_nam + 0x18u), 0x0000u);
    for (uint32_t i = 0u; i < AC97_PERIOD_COUNT; ++i) {
        g_bdl[i].address =
            (uint32_t)(g_periods_phys + (uint64_t)i * AC97_PERIOD_BYTES);
        g_bdl[i].samples = AC97_PERIOD_BYTES / 2u;
        g_bdl[i].control = 0x8000u;
    }
    g_api->hal.io_out32((uint16_t)(g_nabm + 0x10u),
                        (uint32_t)g_bdl_phys);
    g_ready = true;
    return true;
}

static bool ac97_get_info(driver_audio_info_t *info)
{
    if (info == NULL || !g_ready) {
        return false;
    }
    info->sample_rate = 48000u;
    info->channels = 2u;
    info->format = DRIVER_AUDIO_FORMAT_S16_LE;
    info->period_bytes = AC97_PERIOD_BYTES;
    info->period_count = AC97_PERIOD_COUNT;
    return true;
}

static bool ac97_open(void)
{
    if (!g_ready || g_open) {
        return false;
    }
    g_api->hal.io_out8((uint16_t)(g_nabm + 0x1Bu), 0x02u);
    g_api->timer.msleep(1u);
    g_api->hal.io_out16((uint16_t)(g_nabm + 0x16u), 0x001Cu);
    g_open = true;
    return true;
}

static bool ac97_play_period(const uint8_t *pcm, uint32_t bytes)
{
    g_api->mem.memset(g_periods, 0, AC97_PERIOD_BYTES);
    g_api->mem.memcpy(g_periods, pcm, bytes);
    g_bdl[0].samples = (uint16_t)(AC97_PERIOD_BYTES / 2u);
    g_api->hal.io_out8((uint16_t)(g_nabm + 0x1Bu), 0x02u);
    g_api->hal.io_out32((uint16_t)(g_nabm + 0x10u),
                        (uint32_t)g_bdl_phys);
    g_api->hal.io_out8((uint16_t)(g_nabm + 0x15u), 0u);
    g_api->hal.io_out16((uint16_t)(g_nabm + 0x16u), 0x001Cu);
    g_api->hal.io_out8((uint16_t)(g_nabm + 0x1Bu), 0x15u);
    uint64_t start = g_api->timer.monotonic_ns();
    for (;;) {
        uint16_t status =
            g_api->hal.io_in16((uint16_t)(g_nabm + 0x16u));
        if ((status & 0x08u) != 0u) {
            g_api->hal.io_out16((uint16_t)(g_nabm + 0x16u), 0x001Cu);
            return true;
        }
        if ((status & 0x10u) != 0u ||
            g_api->timer.monotonic_ns() - start >
            (uint64_t)AC97_TIMEOUT_MS * 1000000ULL) {
            return false;
        }
        g_api->hal.cpu_pause();
    }
}

static int64_t ac97_write(const void *pcm, uint64_t bytes)
{
    if (!g_open || pcm == NULL || bytes == 0u || (bytes & 3u) != 0u) {
        return -1;
    }
    const uint8_t *source = pcm;
    uint64_t done = 0u;
    while (done < bytes) {
        uint32_t chunk = (uint32_t)(bytes - done);
        if (chunk > AC97_PERIOD_BYTES) {
            chunk = AC97_PERIOD_BYTES;
        }
        if (!ac97_play_period(source + done, chunk)) {
            return done == 0u ? -1 : (int64_t)done;
        }
        done += chunk;
    }
    return (int64_t)done;
}

static bool ac97_drain(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return g_open;
}

static void ac97_close(void)
{
    if (g_ready) {
        g_api->hal.io_out8((uint16_t)(g_nabm + 0x1Bu), 0x00u);
    }
    g_open = false;
}

static bool ac97_is_ready(void) { return g_ready; }

static const driver_audio_t g_audio = {
    .name = "ac97",
    .priority = 30u,
    .init = ac97_init,
    .is_ready = ac97_is_ready,
    .get_info = ac97_get_info,
    .open = ac97_open,
    .write = ac97_write,
    .drain = ac97_drain,
    .close = ac97_close,
};

static void ac97_shutdown(void)
{
    ac97_close();
    g_ready = false;
}

static const driver_module_descriptor_t g_module = {
    .magic = DRIVER_DESCRIPTOR_MAGIC,
    .version = DRIVER_DESCRIPTOR_VERSION,
    .kind = DEVICE_TYPE_AUDIO,
    .load_priority = 45u,
    .deps = { "PCI_Driver.ELF", NULL },
    .driver_api = &g_audio,
    .shutdown = ac97_shutdown,
};

__attribute__((visibility("default")))
const driver_module_descriptor_t *driver_module_init(
    const driver_binary_t *api)
{
    if (api == NULL || api->version_major != DRIVER_API_VERSION_MAJOR ||
        api->pci.get_device_count == NULL || api->mem.dma_alloc_ex == NULL) {
        return NULL;
    }
    g_api = api;
    return &g_module;
}
