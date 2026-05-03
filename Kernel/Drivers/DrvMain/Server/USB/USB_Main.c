#include "USB_Main.h"
#include "EHCI/EHCI.h"
#include "OHCI/OHCI.h"
#include "UHCI/UHCI.h"
#include "XHCI/XHCI.h"
#include "MassStorage/MassStorage.h"
#include "HID/USB_HID.h"
#include "../../../Module/DriverBinary.h"

const driver_binary_t *g_api = NULL;

uint8_t  g_mass_storage_addr      = 0;
uint8_t  g_mass_storage_ep_in     = 0;
uint8_t  g_mass_storage_ep_out    = 0;
uint8_t  g_mass_storage_interface = 0;
uint16_t g_mass_storage_ep_in_mps = 0;
uint16_t g_mass_storage_ep_out_mps= 0;

static uint8_t  g_hid_kbd_addr      = 0;
static uint8_t  g_hid_kbd_interface = 0;
static uint8_t  g_hid_kbd_ep_in     = 0;
static uint16_t g_hid_kbd_ep_mps    = 0;

static uint8_t  g_hid_mouse_addr      = 0;
static uint8_t  g_hid_mouse_interface = 0;
static uint8_t  g_hid_mouse_ep_in     = 0;
static uint16_t g_hid_mouse_ep_mps    = 0;

static usb_hc_type_t g_hc_type = USB_HC_NONE;
static bool g_ohci_ready = false;
static usb_hc_type_t g_dev_hc[256];
static usb_hc_type_t g_default_hc = USB_HC_NONE;
static bool g_uhci_ready = false;

usb_hc_type_t usb_get_hc_type(void) { return g_hc_type; }

static void usb_wait_ms(usb_hc_type_t hc, uint32_t ms)
{
    if (ms == 0) return;
    if (hc == USB_HC_EHCI) ehci_delay_ms(ms);
    else                   xhci_delay_ms(ms);
}

bool usb_submit_control(uint8_t addr,
                        uint8_t bmRequestType, uint8_t bRequest,
                        uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                        void *data)
{
    usb_device_request_t req;
    req.bmRequestType = bmRequestType;
    req.bRequest      = bRequest;
    req.wValue        = wValue;
    req.wIndex        = wIndex;
    req.wLength       = wLength;
    return usb_control_transfer(addr, 0, 64, &req, data);
}

void usb_core_init(void) {
    usb_wait_ms(g_hc_type, 20);

    uint8_t next_addr = 1;
    bool any_device = false;

    uint32_t num_ports = 0;
    if      (g_hc_type == USB_HC_EHCI) num_ports = ehci_get_num_ports();
    else if (g_hc_type == USB_HC_OHCI) num_ports = ohci_get_num_ports();
    else if (g_hc_type == USB_HC_UHCI) num_ports = uhci_get_num_ports();
    else if (g_hc_type == USB_HC_XHCI) num_ports = xhci_get_num_ports();

    if (num_ports == 0) {
        return;
    }

    for (uint32_t i = 0; i < num_ports; i++) {
        bool port_valid = false;
        if      (g_hc_type == USB_HC_EHCI) port_valid = ehci_port_valid(i);
        else if (g_hc_type == USB_HC_OHCI) port_valid = ohci_port_valid(i);
        else if (g_hc_type == USB_HC_UHCI) port_valid = uhci_port_valid(i);
        else if (g_hc_type == USB_HC_XHCI) port_valid = xhci_port_valid(i);
        if (!port_valid) {
            continue;
        }

        usb_hc_type_t port_hc = g_hc_type;
        bool port_ok = false;
        if (g_hc_type == USB_HC_EHCI) {
            port_ok = ehci_reset_port(i);
            if (!port_ok && g_ohci_ready && ohci_reset_port(i)) { port_hc = USB_HC_OHCI; port_ok = true; }
            if (!port_ok && g_uhci_ready && uhci_reset_port(i)) { port_hc = USB_HC_UHCI; port_ok = true; }
        } else if (g_hc_type == USB_HC_OHCI) {
            port_ok = ohci_reset_port(i);
        } else if (g_hc_type == USB_HC_UHCI) {
            port_ok = uhci_reset_port(i);
        } else if (g_hc_type == USB_HC_XHCI) {
            port_ok = xhci_reset_port(i);
        }
        if (!port_ok) continue;

        usb_wait_ms(port_hc, 30);

        bool connected = false;
        if      (port_hc == USB_HC_EHCI) connected = ehci_port_connected(i);
        else if (port_hc == USB_HC_OHCI) connected = ohci_port_connected(i);
        else if (port_hc == USB_HC_UHCI) connected = uhci_port_connected(i);
        else if (port_hc == USB_HC_XHCI) connected = xhci_port_connected(i);
        if (!connected) {
            continue;
        }

        uint8_t current_addr = next_addr++;

        g_default_hc = port_hc;
        g_dev_hc[0] = port_hc;

        usb_set_address(0, current_addr);

        usb_wait_ms(port_hc, 10);

        g_dev_hc[current_addr] = port_hc;

        usb_device_descriptor_t desc = {0};

        int retry_count = 8;
        bool desc_ok = false;
        while (retry_count-- > 0) {
            if (usb_get_device_descriptor(current_addr, &desc)) {
                desc_ok = true;
                break;
            }
            usb_wait_ms(port_hc, 50);
        }
        if (!desc_ok) {
            continue;
        }
        any_device = true;

        if (port_hc == USB_HC_XHCI) {
            uint16_t ep0_mps = desc.bMaxPacketSize0;
            if (ep0_mps == 9) ep0_mps = 512;
            xhci_evaluate_ep0_mps(current_addr, ep0_mps);
        }

        uint8_t conf_buf[256];
        usb_device_request_t req;
        req.bmRequestType = USB_REQ_DIR_IN | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE;
        req.bRequest      = USB_REQ_GET_DESCRIPTOR;
        req.wValue        = (USB_DESC_CONFIGURATION << 8) | 0;
        req.wIndex        = 0;
        req.wLength       = 255;

        uint8_t kbd_last_iface  = 0xFF;
        uint8_t mouse_last_iface = 0xFF;

        if (usb_control_transfer(current_addr, 0, 64, &req, conf_buf)) {
            uint32_t total_len = conf_buf[2] | ((uint32_t)conf_buf[3] << 8);
            uint32_t pos = 0;

            uint8_t current_iface = 0;

        while (pos + 2 <= total_len && pos < sizeof(conf_buf)) {
            uint8_t len  = conf_buf[pos];
            uint8_t type = conf_buf[pos + 1];
            if (len == 0) break;
            if (type == USB_DESC_INTERFACE) {
                current_iface = conf_buf[pos + 2];
                uint8_t iface_class    = conf_buf[pos + 5];
                uint8_t iface_subclass = conf_buf[pos + 6];
                uint8_t iface_protocol = conf_buf[pos + 7];

                if (iface_class == 0x08) {
                   g_mass_storage_addr      = current_addr;
                   g_mass_storage_interface = current_iface;
               } else if (iface_class == 0x03) {
                   if (iface_subclass == 1 && iface_protocol == 1) {
                        g_hid_kbd_addr      = current_addr;
                        g_hid_kbd_interface = current_iface;
                        g_hid_kbd_ep_in     = 0;
                        g_hid_kbd_ep_mps    = 8;
                        kbd_last_iface      = current_iface;
                    } else if (iface_subclass == 1 && iface_protocol == 2) {
                       g_hid_mouse_addr      = current_addr;
                       g_hid_mouse_interface = current_iface;
                       g_hid_mouse_ep_in     = 0;
                       g_hid_mouse_ep_mps    = 8;
                       mouse_last_iface      = current_iface;
                   }
               }
           } else if (type == USB_DESC_ENDPOINT &&
                      (g_mass_storage_addr == current_addr ||
                       g_hid_kbd_addr      == current_addr ||
                         g_hid_mouse_addr    == current_addr)) {
               uint8_t  ep_addr    = conf_buf[pos + 2];
               uint8_t  attributes = conf_buf[pos + 3];
               uint16_t mps = (uint16_t)(conf_buf[pos + 4] |
                                         ((uint16_t)conf_buf[pos + 5] << 8));
               if (g_mass_storage_addr == current_addr &&
                   current_iface == g_mass_storage_interface &&
                   (attributes & 0x03u) == 2) {
                   if (ep_addr & 0x80u) {
                       g_mass_storage_ep_in      = ep_addr & 0x7Fu;
                         g_mass_storage_ep_in_mps  = mps;
                   } else {
                       g_mass_storage_ep_out     = ep_addr & 0x7Fu;
                       g_mass_storage_ep_out_mps = mps;
                   }
                }
                else if (g_hid_kbd_addr == current_addr &&
                        current_iface == kbd_last_iface &&
                        (attributes & 0x03u) == 3 &&
                        (ep_addr & 0x80u)) {
                    g_hid_kbd_ep_in  = ep_addr & 0x7Fu;
                    g_hid_kbd_ep_mps = mps;
                }
                else if (g_hid_mouse_addr == current_addr &&
                        current_iface == mouse_last_iface &&
                        (attributes & 0x03u) == 3 &&
                        (ep_addr & 0x80u)) {
                    g_hid_mouse_ep_in  = ep_addr & 0x7Fu;
                    g_hid_mouse_ep_mps = mps;
               }
            }
                pos += len;
            }

            req.bmRequestType = USB_REQ_DIR_OUT | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE;
            req.bRequest      = USB_REQ_SET_CONFIGURATION;
            req.wValue        = conf_buf[5];
            req.wIndex        = 0;
            req.wLength       = 0;
            usb_control_transfer(current_addr, 0, 64, &req, NULL);

            if (g_hid_kbd_addr == current_addr && g_hid_kbd_ep_in != 0) {
                usb_hid_add_keyboard(g_hid_kbd_addr, g_hid_kbd_interface, g_hid_kbd_ep_in, g_hid_kbd_ep_mps);
            } else if (g_hid_kbd_addr == current_addr) {
            }
            if (g_hid_mouse_addr == current_addr && g_hid_mouse_ep_in != 0) {
                usb_hid_add_mouse(g_hid_mouse_addr, g_hid_mouse_interface, g_hid_mouse_ep_in, g_hid_mouse_ep_mps);
            }
        }
    }

    if (!any_device && g_hc_type == USB_HC_EHCI) {
        if (g_ohci_ready) {
            g_hc_type = USB_HC_OHCI;
            g_default_hc = USB_HC_NONE;
            if (g_api) g_api->memset(g_dev_hc, 0, sizeof(g_dev_hc));
            usb_core_init();
            return;
        } else if (g_uhci_ready) {
            g_hc_type = USB_HC_UHCI;
            g_default_hc = USB_HC_NONE;
            if (g_api) g_api->memset(g_dev_hc, 0, sizeof(g_dev_hc));
            usb_core_init();
            return;
        }
    }
}

bool usb_control_transfer(uint8_t addr, uint8_t endpoint, uint16_t max_packet_size,
                          usb_device_request_t *req, void *data_buffer) {
    usb_hc_type_t hc = (addr == 0) ? g_default_hc : g_dev_hc[addr];
    if (hc == USB_HC_NONE) hc = g_hc_type;
    if (hc == USB_HC_OHCI)
        return ohci_submit_control(addr, endpoint, max_packet_size, req, data_buffer);
    else if (hc == USB_HC_UHCI)
        return uhci_submit_control(addr, endpoint, max_packet_size, req, data_buffer);
    else if (hc == USB_HC_EHCI)
        return ehci_submit_control(addr, endpoint, max_packet_size, req, data_buffer);
    else if (hc == USB_HC_XHCI)
        return xhci_submit_control(addr, endpoint, max_packet_size, req, data_buffer);
    return false;
}

bool usb_submit_bulk(uint8_t addr, uint8_t endpoint, uint16_t max_packet_size,
                     uint8_t pid, void *data, uint32_t length) {
    usb_hc_type_t hc = g_dev_hc[addr];
    if (hc == USB_HC_NONE) hc = g_hc_type;
    if (hc == USB_HC_OHCI)
        return ohci_submit_bulk(addr, endpoint, max_packet_size, pid, data, length);
    else if (hc == USB_HC_UHCI)
        return uhci_submit_bulk(addr, endpoint, max_packet_size, pid, data, length);
    else if (hc == USB_HC_EHCI)
        return ehci_submit_bulk(addr, endpoint, max_packet_size, pid, data, length);
    else if (hc == USB_HC_XHCI)
        return xhci_submit_bulk(addr, endpoint, max_packet_size, pid, data, length);
    return false;
}

bool usb_set_address(uint8_t old_addr, uint8_t new_addr) {
    usb_device_request_t req;
    req.bmRequestType = USB_REQ_DIR_OUT | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE;
    req.bRequest      = USB_REQ_SET_ADDRESS;
    req.wValue        = new_addr;
    req.wIndex        = 0;
    req.wLength       = 0;
    return usb_control_transfer(old_addr, 0, 64, &req, NULL);
}

bool usb_submit_interrupt_in_async(uint8_t addr, uint8_t ep_num,
                                    uint16_t max_packet_size,
                                    void *dma_buf, uint64_t dma_phys,
                                    uint16_t length)
{
    usb_hc_type_t hc = g_dev_hc[addr];
    if (hc == USB_HC_NONE) hc = g_hc_type;
    if (hc == USB_HC_XHCI)
        return xhci_submit_interrupt_in_async(addr, ep_num, max_packet_size, dma_buf, dma_phys, length);
    return false;
}

int usb_check_interrupt_event(uint8_t addr, uint8_t ep_num)
{
    usb_hc_type_t hc = g_dev_hc[addr];
    if (hc == USB_HC_NONE) hc = g_hc_type;
    if (hc == USB_HC_XHCI)
        return xhci_check_interrupt_event(addr, ep_num);
    return -1;
}

bool usb_submit_interrupt_in_sync(uint8_t addr, uint8_t endpoint,
                                   uint16_t max_packet_size,
                                   void *data, uint16_t length)
{
    usb_hc_type_t hc = g_dev_hc[addr];
    if (hc == USB_HC_NONE) hc = g_hc_type;
    if (hc == USB_HC_EHCI)
        return ehci_submit_interrupt_in(addr, endpoint, max_packet_size, data, length);
    else if (hc == USB_HC_OHCI)
        return ohci_submit_interrupt_in(addr, endpoint, max_packet_size, data, length);
    else if (hc == USB_HC_UHCI)
        return uhci_submit_interrupt_in(addr, endpoint, max_packet_size, data, length);
    else if (hc == USB_HC_XHCI) {
        return xhci_submit_interrupt_in_async(addr, endpoint, max_packet_size, data, 0, length);
    }
    return false;
}

bool usb_get_device_descriptor(uint8_t addr, usb_device_descriptor_t *desc) {
    usb_device_request_t req;
    req.bmRequestType = USB_REQ_DIR_IN | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE;
    req.bRequest      = USB_REQ_GET_DESCRIPTOR;
    req.wValue        = (USB_DESC_DEVICE << 8);
    req.wIndex        = 0;
    req.wLength       = sizeof(usb_device_descriptor_t);
    usb_hc_type_t hc = (addr == 0) ? g_default_hc : g_dev_hc[addr];
    if (hc == USB_HC_NONE) hc = usb_get_hc_type();
    uint16_t mps = (hc == USB_HC_EHCI || hc == USB_HC_XHCI) ? 64 : 8;
    return usb_control_transfer(addr, 0, mps, &req, desc);
}

void usb_set_hc_type(usb_hc_type_t type) {
    g_hc_type = type;
}

static bool g_usb_initialized = false;

static void usb_module_init_internal(void)
{
    if (g_usb_initialized) {
        return;
    }
    g_usb_initialized = true;

    if (g_api) g_api->memset(g_dev_hc, 0, sizeof(g_dev_hc));
    g_default_hc = USB_HC_NONE;
    
    xhci_init();
    if (xhci_is_ready()) {
        usb_set_hc_type(USB_HC_XHCI);
    }
    
    ehci_init();
    if (g_hc_type == USB_HC_NONE && ehci_get_num_ports() > 0) {
        usb_set_hc_type(USB_HC_EHCI);
    }

    ohci_init();
    g_ohci_ready = ohci_is_ready();

    uhci_init();
    g_uhci_ready = uhci_is_ready();

    if (g_hc_type == USB_HC_NONE && g_ohci_ready) {
        usb_set_hc_type(USB_HC_OHCI);
    }
    if (g_hc_type == USB_HC_NONE && g_uhci_ready) {
        usb_set_hc_type(USB_HC_UHCI);
    }
    
    usb_core_init();
    usb_hid_init();
    bot_init();
}

static const usb_master_vtable_t g_usb_vtable = {
    .input   = {
        .init           = usb_module_init_internal,
        .poll           = usb_hid_poll,
        .read_keyboard  = usb_hid_read_keyboard,
        .read_mouse     = usb_hid_read_mouse,
        .drain_keyboard = usb_hid_drain_keyboard,
        .drain_mouse    = usb_hid_drain_mouse,
    },
    .storage = {
        .read_sectors   = bot_read_sectors,
        .write_sectors  = bot_write_sectors,
    },
    .usb     = {
        .submit_interrupt_in_async = usb_submit_interrupt_in_async,
        .check_interrupt_event     = usb_check_interrupt_event,
        .submit_interrupt_in_sync  = usb_submit_interrupt_in_sync,
    }
};

static void usb_driver_shutdown(void)
{
    g_usb_initialized = false;
    g_mass_storage_addr = 0;
    g_mass_storage_ep_in = 0;
    g_mass_storage_ep_out = 0;
    g_mass_storage_interface = 0;
    g_mass_storage_ep_in_mps = 0;
    g_mass_storage_ep_out_mps = 0;
    g_hid_kbd_addr = 0;
    g_hid_kbd_interface = 0;
    g_hid_kbd_ep_in = 0;
    g_hid_kbd_ep_mps = 0;
    g_hid_mouse_addr = 0;
    g_hid_mouse_interface = 0;
    g_hid_mouse_ep_in = 0;
    g_hid_mouse_ep_mps = 0;
    g_hc_type = USB_HC_NONE;
    g_ohci_ready = false;
    g_default_hc = USB_HC_NONE;
    g_uhci_ready = false;
    for (uint32_t i = 0; i < 256u; ++i) {
        g_dev_hc[i] = USB_HC_NONE;
    }
    g_api = NULL;
}

static const driver_module_descriptor_t g_usb_module = {
    .driver_api = &g_usb_vtable,
    .shutdown = usb_driver_shutdown,
};

const driver_module_descriptor_t *driver_module_init(const driver_binary_t *api)
{
    if (api == NULL) {
        return NULL;
    }
    g_api = api;
    return &g_usb_module;
}
