#include "MassStorage.h"
#include "../USB_Main.h"
#include "../EHCI/EHCI.h"
#include "../XHCI/XHCI.h"
#include "../../../../Module/DriverBinary.h"

extern const driver_binary_t *g_api;

#define CBW_SIGNATURE 0x43425355
#define CSW_SIGNATURE 0x53425355

typedef struct {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} __attribute__((packed)) usb_bot_cbw_t;

typedef struct {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} __attribute__((packed)) usb_bot_csw_t;

extern uint8_t  g_mass_storage_addr;
extern uint8_t  g_mass_storage_ep_in;
extern uint8_t  g_mass_storage_ep_out;
extern uint8_t  g_mass_storage_interface;
extern uint16_t g_mass_storage_ep_in_mps;
extern uint16_t g_mass_storage_ep_out_mps;

static uint32_t bot_tag        = 1;
static uint32_t bot_block_size = 512;
static uint32_t bot_max_lba    = 0;
static uint8_t  bot_bounce_buffer[65536];

static bool bot_execute_command(void *cbw_cb, uint8_t cb_len, uint8_t dir_in,
                                uint32_t data_len, void *data_buf);

static bool bot_mass_storage_reset(void)
{
    return usb_submit_control(g_mass_storage_addr, 0x21, 0xFF, 0,
                              g_mass_storage_interface, 0, NULL);
}

static void bot_clear_endpoint_halts(void)
{
    usb_submit_control(g_mass_storage_addr, 0x02, 0x01, 0x0000,
                       g_mass_storage_ep_in | 0x80, 0, NULL);

    usb_submit_control(g_mass_storage_addr, 0x02, 0x01, 0x0000,
                       g_mass_storage_ep_out, 0, NULL);
}

static void bot_dump_sense(void)
{
    uint8_t cb[16] = {0};
    cb[0] = 0x03;
    cb[4] = 18;

    uint8_t buf[18] = {0};
    if (!bot_execute_command(cb, 6, 1, 18, buf)) return;
}

bool bot_test_unit_ready(void)
{
    uint8_t cb[16] = {0};
    cb[0] = 0x00;
    return bot_execute_command(cb, 6, 0, 0, NULL);
}

bool bot_request_sense(void)
{
    uint8_t cb[16] = {0};
    cb[0] = 0x03;
    cb[4] = 18;
    uint8_t buf[18] = {0};
    return bot_execute_command(cb, 6, 1, 18, buf);
}

bool bot_inquiry(void)
{
    uint8_t cb[16] = {0};
    cb[0] = 0x12;
    cb[4] = 36;
    uint8_t buf[36] = {0};
    return bot_execute_command(cb, 6, 1, 36, buf);
}

bool bot_read_capacity(void)
{
    uint8_t cb[16] = {0};
    cb[0] = 0x25;
    uint8_t buf[8] = {0};
    if (!bot_execute_command(cb, 10, 1, 8, buf)) return false;

    bot_max_lba    = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
                   | ((uint32_t)buf[2] <<  8) |  (uint32_t)buf[3];
    bot_block_size = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16)
                   | ((uint32_t)buf[6] <<  8) |  (uint32_t)buf[7];

    if (bot_block_size == 0) bot_block_size = 512;

    return true;
}

bool bot_init(void)
{
    if (g_mass_storage_addr == 0 ||
        g_mass_storage_ep_in == 0 ||
        g_mass_storage_ep_out == 0) {
        return false;
    }

    g_mass_storage_ep_in  &= 0x0F;
    g_mass_storage_ep_out &= 0x0F;

    if (g_mass_storage_ep_in_mps  == 0) g_mass_storage_ep_in_mps  = 512;
    if (g_mass_storage_ep_out_mps == 0) g_mass_storage_ep_out_mps = 512;

    bot_mass_storage_reset();
    bot_clear_endpoint_halts();

    if (usb_get_hc_type() == USB_HC_XHCI) xhci_delay_ms(100);
    else                                   ehci_delay_ms(100);

    bot_inquiry();

    for (int i = 0; i < 5; i++) {
        if (bot_test_unit_ready()) break;
        bot_request_sense();
        if (usb_get_hc_type() == USB_HC_XHCI) xhci_delay_ms(200);
        else                                   ehci_delay_ms(200);
    }

    if (!bot_read_capacity()) return false;

    for (int i = 0; i < 3; i++) {
        if (bot_test_unit_ready()) break;
        bot_request_sense();
        if (usb_get_hc_type() == USB_HC_XHCI) xhci_delay_ms(100);
        else                                   ehci_delay_ms(100);
    }

    if (usb_get_hc_type() == USB_HC_XHCI) xhci_delay_ms(200);
    else                                   ehci_delay_ms(200);

    return true;
}

static bool bot_execute_command(void *cbw_cb, uint8_t cb_len, uint8_t dir_in,
                                uint32_t data_len, void *data_buf)
{
    usb_bot_cbw_t cbw = {0};
    uint32_t tag = bot_tag++;

    cbw.dCBWSignature          = CBW_SIGNATURE;
    cbw.dCBWTag                = tag;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags             = dir_in ? 0x80 : 0x00;
    cbw.bCBWLUN                = 0;
    cbw.bCBWCBLength           = cb_len;

    if (g_api) g_api->memcpy(cbw.CBWCB, cbw_cb, cb_len);

    if (!usb_submit_bulk(g_mass_storage_addr,
                         g_mass_storage_ep_out,
                         g_mass_storage_ep_out_mps,
                         0,
                         &cbw, sizeof(cbw))) {
        return false;
    }

    if (data_len > 0 && data_buf != NULL) {
        uint8_t  pid = dir_in ? 1 : 0;
        uint8_t  ep  = dir_in ? g_mass_storage_ep_in : g_mass_storage_ep_out;
        uint16_t mps = dir_in ? g_mass_storage_ep_in_mps : g_mass_storage_ep_out_mps;

        if (!usb_submit_bulk(g_mass_storage_addr, ep, mps, pid, data_buf, data_len)) {
            return false;
        }
    }

    usb_bot_csw_t csw = {0};

    if (!usb_submit_bulk(g_mass_storage_addr,
                         g_mass_storage_ep_in,
                         g_mass_storage_ep_in_mps,
                         1,
                         &csw, sizeof(csw))) {
        return false;
    }

    if (csw.dCSWSignature != CSW_SIGNATURE) {
        return false;
    }
    if (csw.dCSWTag != tag) {
        return false;
    }

    if (csw.bCSWStatus != 0) {
        bot_dump_sense();
        return false;
    }

    return true;
}

bool bot_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t sectors)
{
    if (g_mass_storage_addr == 0 || g_mass_storage_ep_in == 0 || g_mass_storage_ep_out == 0) return false;

    uint32_t total_sectors_done = 0;

    while (total_sectors_done < sectors) {
        uint32_t current_lba = lba + total_sectors_done;
        uint32_t remaining = sectors - total_sectors_done;

        uint32_t factor = (bot_block_size > 512) ? (bot_block_size / 512) : 1;
        uint32_t phys_lba = current_lba / factor;
        uint32_t max_phys_blocks = sizeof(bot_bounce_buffer) / (factor * 512);
        if (max_phys_blocks == 0) return false;

        uint32_t phys_end = (current_lba + remaining - 1) / factor;
        uint32_t phys_count = phys_end - phys_lba + 1;

        if (phys_count > max_phys_blocks) {
            phys_count = max_phys_blocks;
            uint32_t end_sector = (phys_lba + phys_count) * factor - 1;
            uint32_t chunk_sectors = end_sector - current_lba + 1;
            if (chunk_sectors > remaining) chunk_sectors = remaining;
            remaining = chunk_sectors;
        }

        bool chunk_ok = false;
        for (int retry = 0; retry < 3; retry++) {
            uint8_t cb[16] = {0};
            cb[0] = 0x28;
            cb[2] = (uint8_t)(phys_lba >> 24);
            cb[3] = (uint8_t)(phys_lba >> 16);
            cb[4] = (uint8_t)(phys_lba >>  8);
            cb[5] = (uint8_t)(phys_lba);
            cb[7] = (uint8_t)(phys_count >> 8);
            cb[8] = (uint8_t)(phys_count);

            bool result = bot_execute_command(cb, 10, 1,
                                              phys_count * (factor * 512),
                                              bot_bounce_buffer);

            if (result) {
                uint32_t offset = (current_lba % factor) * 512;
                if (g_api) g_api->memcpy(buffer + total_sectors_done * 512,
                                         bot_bounce_buffer + offset,
                                         remaining * 512);
                chunk_ok = true;
                break;
            }

            bot_request_sense();
            bot_clear_endpoint_halts();
            if (usb_get_hc_type() == USB_HC_XHCI) xhci_delay_ms(50);
            else                                   ehci_delay_ms(50);
            bot_test_unit_ready();
        }

        if (!chunk_ok) return false;
        total_sectors_done += remaining;
    }

    return true;
}

bool bot_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t sectors)
{
    if (g_mass_storage_addr == 0 || g_mass_storage_ep_in == 0 || g_mass_storage_ep_out == 0) return false;

    uint32_t total_sectors_done = 0;

    while (total_sectors_done < sectors) {
        uint32_t current_lba = lba + total_sectors_done;
        uint32_t remaining = sectors - total_sectors_done;

        uint32_t factor = (bot_block_size > 512) ? (bot_block_size / 512) : 1;
        uint32_t phys_lba = current_lba / factor;
        uint32_t max_phys_blocks = sizeof(bot_bounce_buffer) / (factor * 512);
        if (max_phys_blocks == 0) return false;

        uint32_t phys_end = (current_lba + remaining - 1) / factor;
        uint32_t phys_count = phys_end - phys_lba + 1;

        if (phys_count > max_phys_blocks) {
            phys_count = max_phys_blocks;
            uint32_t end_sector = (phys_lba + phys_count) * factor - 1;
            uint32_t chunk_sectors = end_sector - current_lba + 1;
            if (chunk_sectors > remaining) chunk_sectors = remaining;
            remaining = chunk_sectors;
        }

        bool chunk_ok = false;
        for (int retry = 0; retry < 3; retry++) {
            uint32_t offset = (current_lba % factor) * 512;
            
            if (factor > 1 && (offset != 0 || remaining < phys_count * factor)) {
                uint8_t cb_rd[16] = {0};
                cb_rd[0] = 0x28;
                cb_rd[2] = (uint8_t)(phys_lba >> 24);
                cb_rd[3] = (uint8_t)(phys_lba >> 16);
                cb_rd[4] = (uint8_t)(phys_lba >>  8);
                cb_rd[5] = (uint8_t)(phys_lba);
                cb_rd[7] = (uint8_t)(phys_count >> 8);
                cb_rd[8] = (uint8_t)(phys_count);
                if (!bot_execute_command(cb_rd, 10, 1, phys_count * (factor * 512), bot_bounce_buffer)) {
                    continue;
                }
            } else {
                if (g_api) g_api->memset(bot_bounce_buffer, 0, phys_count * (factor * 512));
            }

            if (g_api) g_api->memcpy(bot_bounce_buffer + offset,
                                     buffer + total_sectors_done * 512,
                                     remaining * 512);

            uint8_t cb[16] = {0};
            cb[0] = 0x2A;
            cb[2] = (uint8_t)(phys_lba >> 24);
            cb[3] = (uint8_t)(phys_lba >> 16);
            cb[4] = (uint8_t)(phys_lba >>  8);
            cb[5] = (uint8_t)(phys_lba);
            cb[7] = (uint8_t)(phys_count >> 8);
            cb[8] = (uint8_t)(phys_count);

            bool result = bot_execute_command(cb, 10, 0,
                                              phys_count * (factor * 512),
                                              bot_bounce_buffer);

            if (result) {
                chunk_ok = true;
                break;
            }

            bot_request_sense();
            bot_clear_endpoint_halts();
            if (usb_get_hc_type() == USB_HC_XHCI) xhci_delay_ms(50);
            else                                   ehci_delay_ms(50);
            bot_test_unit_ready();
        }

        if (!chunk_ok) return false;
        total_sectors_done += remaining;
    }

    return true;
}