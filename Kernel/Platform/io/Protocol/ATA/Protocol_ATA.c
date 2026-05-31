#include "Protocol_ATA.h"
#include "../../IO_Main.h"
#include <string.h>
#include "Drivers/Module/DriverManager.h"
#include "Debug/serial/Serial.h"
#include "Debug/printf/printf.h"

#define ATA_PRI_DATA     0x1F0u
#define ATA_PRI_SECCOUNT 0x1F2u
#define ATA_PRI_LBA0     0x1F3u
#define ATA_PRI_LBA1     0x1F4u
#define ATA_PRI_LBA2     0x1F5u
#define ATA_PRI_HDDEVSEL 0x1F6u
#define ATA_PRI_COMMAND  0x1F7u
#define ATA_PRI_STATUS   0x1F7u
#define ATA_PRI_CONTROL  0x3F6u

#define ATA_SEC_DATA     0x170u
#define ATA_SEC_SECCOUNT 0x172u
#define ATA_SEC_LBA0     0x173u
#define ATA_SEC_LBA1     0x174u
#define ATA_SEC_LBA2     0x175u
#define ATA_SEC_HDDEVSEL 0x176u
#define ATA_SEC_COMMAND  0x177u
#define ATA_SEC_STATUS   0x177u
#define ATA_SEC_CONTROL  0x376u

#define ATA_CMD_READ  0x20u
#define ATA_CMD_WRITE 0x30u
#define ATA_SR_BSY    0x80u
#define ATA_SR_DRQ    0x08u
#define ATA_SR_ERR    0x01u

static uint16_t g_ata_data     = ATA_PRI_DATA;
static uint16_t g_ata_seccount = ATA_PRI_SECCOUNT;
static uint16_t g_ata_lba0     = ATA_PRI_LBA0;
static uint16_t g_ata_lba1     = ATA_PRI_LBA1;
static uint16_t g_ata_lba2     = ATA_PRI_LBA2;
static uint16_t g_ata_hddevsel = ATA_PRI_HDDEVSEL;
static uint16_t g_ata_command  = ATA_PRI_COMMAND;
static uint16_t g_ata_status   = ATA_PRI_STATUS;
static uint16_t g_ata_control  = ATA_PRI_CONTROL;
static uint16_t g_ata_feature  = ATA_PRI_DATA + 1;
static uint8_t  g_ata_devsel_value = 0xA0u;

static bool g_disk_io_working = false;
static bool g_atapi = false;
static uint16_t g_atapi_sector_bytes = 2048u;

static uint8_t g_atapi_scratch[2048] __attribute__((aligned(2)));

static uint32_t g_last_atapi_block = 0xFFFFFFFFu;
static uint8_t  g_atapi_cache_buf[2048] __attribute__((aligned(2)));

typedef struct {
    uint16_t cmd_io;
    uint16_t ctrl_io;
} ata_channel_t;

typedef struct {
    const ata_channel_t *ch;
    uint8_t devsel;
    bool is_atapi;
    uint64_t total_bytes;
} ata_device_t;

static ata_device_t g_ata_devices[4];
static uint32_t g_ata_device_count = 0;
static uint32_t g_ata_current_device = 0;

static ata_channel_t g_primary = { ATA_PRI_DATA, ATA_PRI_CONTROL };
static ata_channel_t g_secondary = { ATA_SEC_DATA, ATA_SEC_CONTROL };

static void ata_delay(uint16_t ctrl_port);
static void ata_set_channel(const ata_channel_t *ch);
static bool ata_probe_device(uint8_t devsel, uint64_t *out_size);
static bool ata_pio_read_chunk(uint32_t real_lba, uint8_t *buffer, uint8_t sector_count);
static bool ata_pio_write_chunk(uint32_t real_lba, const uint8_t *buffer, uint8_t sector_count);

uint32_t ata_get_device_count(void) {
    return g_ata_device_count;
}

bool ata_select_device(uint32_t index) {
    if (index >= g_ata_device_count) return false;
    g_ata_current_device = index;
    ata_set_channel(g_ata_devices[index].ch);
    g_ata_devsel_value = g_ata_devices[index].devsel;
    g_atapi = g_ata_devices[index].is_atapi;
    return true;
}

uint64_t ata_get_total_bytes(void) {
    if (g_ata_device_count == 0) return 0;
    return g_ata_devices[g_ata_current_device].total_bytes;
}

static void ata_set_channel(const ata_channel_t *ch) {
    g_ata_data     = ch->cmd_io;
    g_ata_feature  = ch->cmd_io + 0x01u;
    g_ata_seccount = ch->cmd_io + 0x02u;
    g_ata_lba0     = ch->cmd_io + 0x03u;
    g_ata_lba1     = ch->cmd_io + 0x04u;
    g_ata_lba2     = ch->cmd_io + 0x05u;
    g_ata_hddevsel = ch->cmd_io + 0x06u;
    g_ata_command  = ch->cmd_io + 0x07u;
    g_ata_status   = ch->cmd_io + 0x07u;
    g_ata_control  = ch->ctrl_io;
}

static uint16_t mask_io_bar(uint32_t bar) {
    return (uint16_t)(bar & 0xFFFCu);
}

static void ide_configure_ports_from_pci(void) {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t class_reg = pci_read_config((uint8_t)bus, dev, func, 0x08);
                uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
                uint8_t subclass   = (uint8_t)((class_reg >> 16) & 0xFFu);
                if (class_code != 0x01u || subclass != 0x01u) {
                    if (func == 0) {
                        uint32_t header = pci_read_config((uint8_t)bus, dev, func, 0x0C);
                        if (((header >> 16) & 0x80u) == 0u) break;
                    }
                    continue;
                }

                uint32_t bar0 = pci_read_config((uint8_t)bus, dev, func, 0x10);
                uint32_t bar1 = pci_read_config((uint8_t)bus, dev, func, 0x14);
                uint32_t bar2 = pci_read_config((uint8_t)bus, dev, func, 0x18);
                uint32_t bar3 = pci_read_config((uint8_t)bus, dev, func, 0x1C);
                uint32_t cmd  = pci_read_config((uint8_t)bus, dev, func, 0x04);

                pci_write_config((uint8_t)bus, dev, func, 0x04, cmd | 0x5u);

                ata_channel_t primary = g_primary;
                ata_channel_t secondary = g_secondary;

                if (bar0 != 0 && (bar0 & 0x1u)) primary.cmd_io = mask_io_bar(bar0);
                if (bar1 != 0 && (bar1 & 0x1u)) primary.ctrl_io = (uint16_t)(mask_io_bar(bar1));
                if (bar2 != 0 && (bar2 & 0x1u)) secondary.cmd_io = mask_io_bar(bar2);
                if (bar3 != 0 && (bar3 & 0x1u)) secondary.ctrl_io = (uint16_t)(mask_io_bar(bar3));

                g_primary = primary;
                g_secondary = secondary;
                return;
            }
        }
    }
}

static int ata_poll(uint32_t timeout)
{
    while (timeout--) {
        uint8_t st = inb(g_ata_status);
        if (st == 0xFFu || st == 0x7Fu) return -1;
        if (st & ATA_SR_BSY) continue;
        if (st & ATA_SR_ERR) return -1;
        if (st & ATA_SR_DRQ) return 0;
    }
    return -1;
}

static int atapi_read_capacity(uint64_t *out_size)
{
    uint8_t packet[12] = {0};
    packet[0] = 0x25; // READ CAPACITY

    outb(g_ata_hddevsel, g_ata_devsel_value);
    ata_delay(g_ata_control);

    uint32_t timeout = 1000000u;
    while (inb(g_ata_status) & (ATA_SR_BSY | ATA_SR_DRQ)) {
        if (--timeout == 0u) return -1;
    }

    outb(g_ata_feature, 0);
    outb(g_ata_seccount, 0);
    outb(g_ata_lba0, 0);
    outb(g_ata_lba1, 8); // Expecting 8 bytes
    outb(g_ata_lba2, 0);
    outb(g_ata_command, 0xA0u);

    if (ata_poll(1000000u) < 0) return -1;

    const uint16_t *pktw = (const uint16_t *)packet;
    for (int i = 0; i < 6; ++i) outw(g_ata_data, pktw[i]);

    if (ata_poll(1000000u) < 0) return -1;

    uint32_t data[2];
    data[0] = inw(g_ata_data);
    data[0] |= (uint32_t)inw(g_ata_data) << 16;
    data[1] = inw(g_ata_data);
    data[1] |= (uint32_t)inw(g_ata_data) << 16;

    // Byte swap (big endian to little endian)
    uint32_t max_lba = ((data[0] & 0xFF000000u) >> 24) |
                       ((data[0] & 0x00FF0000u) >> 8) |
                       ((data[0] & 0x0000FF00u) << 8) |
                       ((data[0] & 0x000000FFu) << 24);
    uint32_t block_size = ((data[1] & 0xFF000000u) >> 24) |
                          ((data[1] & 0x00FF0000u) >> 8) |
                          ((data[1] & 0x0000FF00u) << 8) |
                          ((data[1] & 0x000000FFu) << 24);

    if (out_size) *out_size = (uint64_t)(max_lba + 1) * block_size;
    return 0;
}

static int atapi_identify(uint64_t *out_size)
{
    outb(g_ata_hddevsel, g_ata_devsel_value);
    ata_delay(g_ata_control);

    uint32_t timeout = 1000000u;
    while (inb(g_ata_status) & ATA_SR_BSY) {
        if (--timeout == 0u) return -1;
    }

    uint8_t pre = inb(g_ata_status);
    if (pre == 0xFFu || pre == 0x7Fu) {
        return -1;
    }

    outb(g_ata_seccount, 0);
    outb(g_ata_lba0, 0);
    outb(g_ata_lba1, 0);
    outb(g_ata_lba2, 0);
    outb(g_ata_command, 0xA1u);

    timeout = 1000000u;
    while (timeout--) {
        uint8_t st = inb(g_ata_status);
        if (st == 0xFFu || st == 0x7Fu) return -1;
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) {
            for (int i = 0; i < 256; ++i) (void)inw(g_ata_data);
            g_atapi = true;
            g_atapi_sector_bytes = 2048u;
            if (out_size) *out_size = 0;
            atapi_read_capacity(out_size);
            return 0;
        }
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_ERR)) {
            return -1;
        }
    }

    return -1;
}

static int ata_identify(uint64_t *out_size)
{
    outb(g_ata_hddevsel, g_ata_devsel_value);
    ata_delay(g_ata_control);

    uint32_t timeout = 1000000u;
    while (inb(g_ata_status) & ATA_SR_BSY) {
        if (--timeout == 0u) return -1;
    }

    outb(g_ata_seccount, 0);
    outb(g_ata_lba0, 0);
    outb(g_ata_lba1, 0);
    outb(g_ata_lba2, 0);
    outb(g_ata_command, 0xECu);

    if (ata_poll(1000000u) < 0) return -1;

    uint16_t words[256];
    for (int i = 0; i < 256; ++i) words[i] = inw(g_ata_data);

    uint16_t word0 = words[0];

    if (word0 & 0x8000u) {
        g_atapi = true;
        g_atapi_sector_bytes = 2048u;
        if (out_size) *out_size = 0;
        return 0;
    }

    uint32_t lba28 = ((uint32_t)words[61] << 16) | words[60];

    if (lba28 == 0) {
        return -1;
    }

    if (out_size) *out_size = (uint64_t)lba28 * 512u;

    g_atapi = false;
    return 0;
}


static int atapi_read_block(uint32_t lba2048, uint8_t *buf)
{
    if (lba2048 == g_last_atapi_block) {
        if (buf != g_atapi_cache_buf) {
            memcpy(buf, g_atapi_cache_buf, g_atapi_sector_bytes);
        }
        return 0;
    }

    uint8_t packet[12] = {0};
    packet[0] = 0xA8;
    packet[2] = (uint8_t)((lba2048 >> 24) & 0xFFu);
    packet[3] = (uint8_t)((lba2048 >> 16) & 0xFFu);
    packet[4] = (uint8_t)((lba2048 >> 8)  & 0xFFu);
    packet[5] = (uint8_t)(lba2048 & 0xFFu);
    packet[6] = 0;
    packet[7] = 0;
    packet[8] = 0;
    packet[9] = 1;

    outb(g_ata_hddevsel, g_ata_devsel_value);
    ata_delay(g_ata_control);

    
    uint32_t timeout_bsy = 10000000u;
    while (inb(g_ata_status) & (ATA_SR_BSY | ATA_SR_DRQ)) {
        if (--timeout_bsy == 0u) return -1;
    }

    outb(g_ata_feature, 0);
    outb(g_ata_seccount, 0);
    outb(g_ata_lba0, 0);
    outb(g_ata_lba1, (uint8_t)(g_atapi_sector_bytes & 0xFFu));
    outb(g_ata_lba2, (uint8_t)(g_atapi_sector_bytes >> 8));
    outb(g_ata_command, 0xA0u);

    if (ata_poll(10000000u) < 0) {
        return -1;
    }

    const uint16_t *pktw = (const uint16_t *)packet;
    for (int i = 0; i < 6; ++i) {
        outw(g_ata_data, pktw[i]);
    }

    if (ata_poll(10000000u) < 0) {
        return -1;
    }

    uint16_t *w = (uint16_t *)g_atapi_cache_buf;
    for (uint32_t i = 0; i < (g_atapi_sector_bytes / 2u); ++i) {
        w[i] = inw(g_ata_data);
    }
    
    
    inb(g_ata_status);

    if (buf != g_atapi_cache_buf) {
        memcpy(buf, g_atapi_cache_buf, g_atapi_sector_bytes);
    }
    g_last_atapi_block = lba2048;
    
    return 0;
}

static void ata_delay(uint16_t ctrl_port) {
    inb(ctrl_port); inb(ctrl_port);
    inb(ctrl_port); inb(ctrl_port);
}

static void ata_soft_reset(uint16_t ctrl_port) {
    outb(ctrl_port, 0x04u);
    for (int i = 0; i < 14; i++) inb(ctrl_port);
    outb(ctrl_port, 0x00u);
    for (int i = 0; i < 5000; i++) inb(ctrl_port);
}

static bool ata_probe_device(uint8_t devsel_value, uint64_t *out_size) {
    g_ata_devsel_value = devsel_value;
    g_atapi = false;

    outb(g_ata_hddevsel, g_ata_devsel_value);
    ata_delay(g_ata_control);
    ata_soft_reset(g_ata_control);
    outb(g_ata_hddevsel, g_ata_devsel_value);
    ata_delay(g_ata_control);

    uint32_t timeout = 5000000u;
    while (1) {
        uint8_t st = inb(g_ata_status);
        if (st == 0xFFu || st == 0x7Fu) return false;
        if (!(st & ATA_SR_BSY)) break;
        if (--timeout == 0u) return false;
    }

    for (int i = 0; i < 50000; i++) inb(g_ata_control);

    uint8_t st = inb(g_ata_status);
    if (st == 0xFFu || st == 0x7Fu) {
        return false;
    }

    uint8_t cl = inb(g_ata_lba1);
    uint8_t ch = inb(g_ata_lba2);

    if (cl == 0xFFu && ch == 0xFFu) return false;
    if (cl == 0x7Fu && ch == 0x7Fu) return false;

    bool maybe_atapi = ((cl == 0x14u && ch == 0xEBu) ||
                        (cl == 0x69u && ch == 0x96u));

    if (maybe_atapi) {
        if (atapi_identify(out_size) == 0) return true;
        ata_soft_reset(g_ata_control);
        outb(g_ata_hddevsel, g_ata_devsel_value);
        ata_delay(g_ata_control);
        return (atapi_identify(out_size) == 0);
    }

    if (ata_identify(out_size) == 0) {
        if (g_atapi) return true;
        return true;
    }

    ata_soft_reset(g_ata_control);
    outb(g_ata_hddevsel, g_ata_devsel_value);
    ata_delay(g_ata_control);
    timeout = 1000000u;
    while (inb(g_ata_status) & ATA_SR_BSY) {
        if (--timeout == 0u) return false;
    }
    return (atapi_identify(out_size) == 0);
}

bool ata_init(uint64_t partition_lba) {
    (void)partition_lba;
    g_disk_io_working = false;
    g_ata_device_count = 0;

    ide_configure_ports_from_pci();

    typedef struct { const ata_channel_t *ch; uint8_t devsel; } slot_t;
    slot_t slots[4] = {
        { &g_primary,   0xA0u },
        { &g_primary,   0xB0u },
        { &g_secondary, 0xA0u },
        { &g_secondary, 0xB0u },
    };

    for (int i = 0; i < 4; i++) {
        ata_soft_reset(slots[i].ch->ctrl_io);
        ata_set_channel(slots[i].ch);

        uint64_t size = 0;
        if (!ata_probe_device(slots[i].devsel, &size)) {
            continue;
        }

        if (g_ata_device_count < 4) {
            g_ata_devices[g_ata_device_count].ch = slots[i].ch;
            g_ata_devices[g_ata_device_count].devsel = slots[i].devsel;
            g_ata_devices[g_ata_device_count].is_atapi = g_atapi;
            g_ata_devices[g_ata_device_count].total_bytes = size;
            g_ata_device_count++;
        }
    }

    if (g_ata_device_count > 0) {
        ata_select_device(0);
        g_disk_io_working = true;
        return true;
    }
    
    return false;
}

bool ata_read(uint32_t lba, uint8_t *buffer, uint32_t sectors) {
    if (sectors == 0 || buffer == 0) return false;

    if (g_atapi) {
        for (uint32_t s = 0; s < sectors; ++s) {
            uint64_t byte_off = ((uint64_t)(lba + s) * 512ULL);
            uint32_t block        = (uint32_t)(byte_off / g_atapi_sector_bytes);
            uint32_t off_in_block = (uint32_t)(byte_off % g_atapi_sector_bytes);

            if (atapi_read_block(block, g_atapi_scratch) < 0) {
                return false;
            }
            memcpy(buffer + (s * 512u),
                   g_atapi_scratch + off_in_block,
                   512u);
        }
        g_disk_io_working = true;
        return true;
    }

    uint32_t done = 0;
    while (done < sectors) {
        uint8_t chunk_sectors =
            (uint8_t)((sectors - done) > 255u ? 255u : (sectors - done));
        uint32_t real_lba = lba + done;

        if (!ata_pio_read_chunk(real_lba, buffer, chunk_sectors)) {
            return false;
        }

        buffer += (uint32_t)chunk_sectors * 512u;
        done += chunk_sectors;
    }

    g_disk_io_working = true;
    return true;
}

bool ata_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors) {
    if (sectors == 0 || buffer == 0) return false;

    if (g_atapi) {
        return false;
    }

    uint32_t done = 0;
    while (done < sectors) {
        uint8_t chunk_sectors =
            (uint8_t)((sectors - done) > 255u ? 255u : (sectors - done));
        uint32_t real_lba = lba + done;

        if (!ata_pio_write_chunk(real_lba, buffer, chunk_sectors)) {
            return false;
        }

        buffer += (uint32_t)chunk_sectors * 512u;
        done += chunk_sectors;
    }
    g_disk_io_working = true;
    return true;
}

static bool ata_pio_read_chunk(uint32_t real_lba, uint8_t *buffer, uint8_t sector_count)
{
    if (sector_count == 0u) return false;

    uint8_t devsel = (uint8_t)(0xE0u | (g_ata_devsel_value & 0x10u) |
                               ((real_lba >> 24u) & 0x0Fu));
    outb(g_ata_hddevsel, devsel);
    ata_delay(g_ata_control);
    outb(g_ata_seccount, sector_count);
    outb(g_ata_lba0,     (uint8_t)(real_lba & 0xFFu));
    outb(g_ata_lba1,     (uint8_t)((real_lba >> 8u) & 0xFFu));
    outb(g_ata_lba2,     (uint8_t)((real_lba >> 16u) & 0xFFu));
    outb(g_ata_command,  ATA_CMD_READ);

    for (uint32_t sector = 0; sector < sector_count; ++sector) {
        uint32_t timeout = 10000000u;
        while (inb(g_ata_status) & ATA_SR_BSY) {
            if (--timeout == 0u) return false;
        }

        timeout = 10000000u;
        uint8_t st;
        do {
            st = inb(g_ata_status);
            if (st & ATA_SR_ERR) return false;
            if (--timeout == 0u) return false;
        } while ((st & ATA_SR_DRQ) == 0u);

        uint16_t *dst = (uint16_t *)buffer;
        for (int i = 0; i < 256; ++i) {
            dst[i] = inw(g_ata_data);
        }
        buffer += 512u;
    }

    return true;
}

static bool ata_pio_write_chunk(uint32_t real_lba, const uint8_t *buffer, uint8_t sector_count)
{
    if (sector_count == 0u) return false;

    uint8_t devsel = (uint8_t)(0xE0u | (g_ata_devsel_value & 0x10u) |
                               ((real_lba >> 24u) & 0x0Fu));
    outb(g_ata_hddevsel, devsel);
    ata_delay(g_ata_control);
    outb(g_ata_seccount, sector_count);
    outb(g_ata_lba0,     (uint8_t)(real_lba & 0xFFu));
    outb(g_ata_lba1,     (uint8_t)((real_lba >> 8u) & 0xFFu));
    outb(g_ata_lba2,     (uint8_t)((real_lba >> 16u) & 0xFFu));
    outb(g_ata_command,  ATA_CMD_WRITE);

    for (uint32_t sector = 0; sector < sector_count; ++sector) {
        uint32_t timeout = 10000000u;
        while (inb(g_ata_status) & ATA_SR_BSY) {
            if (--timeout == 0u) return false;
        }

        timeout = 10000000u;
        while ((inb(g_ata_status) & ATA_SR_DRQ) == 0u) {
            if (--timeout == 0u) return false;
        }

        outsw(g_ata_data, buffer, 256);
        buffer += 512u;
    }

    return true;
}

bool ata_is_working(void) {
    return g_disk_io_working;
}