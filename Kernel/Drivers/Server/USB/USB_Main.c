#include "USB_Main.h"
#include "EHCI/EHCI.h"
#include "OHCI/OHCI.h"
#include "UHCI/UHCI.h"
#include "XHCI/XHCI.h"
#include "MassStorage/MassStorage.h"
#include "HID/USB_HID.h"
#include "Drivers/Module/DriverBinary.h"
#include "Debug/serial/Serial.h"

const driver_binary_t *g_api = NULL;

uint8_t  g_mass_storage_addr      = 0;
uint8_t  g_mass_storage_ep_in     = 0;
uint8_t  g_mass_storage_ep_out    = 0;
uint8_t  g_mass_storage_interface = 0;
uint16_t g_mass_storage_ep_in_mps = 0;
uint16_t g_mass_storage_ep_out_mps= 0;

uint8_t  g_enum_speed = 0;
uint8_t  g_enum_parent_hub_addr = 0;
uint8_t  g_enum_parent_port = 0;
uint8_t  g_dev_root_port[256] = {0};
uint32_t g_dev_route_string[256] = {0};

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

#define USB_DESC_HUB 0x29

#define USB_PORT_STATUS_CONNECTION   (1u << 0)
#define USB_PORT_STATUS_ENABLE       (1u << 1)
#define USB_PORT_STATUS_SUSPEND      (1u << 2)
#define USB_PORT_STATUS_RESET        (1u << 4)
#define USB_PORT_STATUS_POWER        (1u << 8)

#define USB_FEATURE_PORT_CONNECTION  0
#define USB_FEATURE_PORT_ENABLE      1
#define USB_FEATURE_PORT_SUSPEND     2
#define USB_FEATURE_PORT_OVER_CURRENT 3
#define USB_FEATURE_PORT_RESET       4
#define USB_FEATURE_PORT_POWER       8
#define USB_FEATURE_C_PORT_CONNECTION 16
#define USB_FEATURE_C_PORT_RESET     20

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bNbrPorts;
    uint16_t wHubCharacteristics;
    uint8_t  bPwrOn2PwrGood;
    uint8_t  bHubContrCurrent;
    uint8_t  bDeviceRemovable;
    uint8_t  bPortPwrCtrlMask;
} __attribute__((packed)) usb_hub_descriptor_t;

static uint16_t usb_le16(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
}

static uint16_t usb_device_max_packet_size(uint8_t packet_size)
{
    return (packet_size == 9) ? 512 : packet_size;
}

static bool usb_hub_get_descriptor(uint8_t addr, uint16_t max_packet_size, usb_hub_descriptor_t *hub_desc)
{
    usb_device_request_t req;
    req.bmRequestType = USB_REQ_DIR_IN | USB_REQ_TYPE_CLASS | USB_REQ_RCPT_DEVICE;
    req.bRequest      = USB_REQ_GET_DESCRIPTOR;
    req.wValue        = (USB_DESC_HUB << 8) | 0;
    req.wIndex        = 0;
    req.wLength       = sizeof(usb_hub_descriptor_t);
    return usb_control_transfer(addr, 0, max_packet_size, &req, hub_desc);
}

static bool usb_hub_get_port_status(uint8_t addr, uint8_t port, uint16_t max_packet_size,
                                   uint16_t *status, uint16_t *change)
{
    uint8_t buffer[4];
    usb_device_request_t req;
    req.bmRequestType = USB_REQ_DIR_IN | USB_REQ_TYPE_CLASS | USB_REQ_RCPT_OTHER;
    req.bRequest      = USB_REQ_GET_STATUS;
    req.wValue        = 0;
    req.wIndex        = port;
    req.wLength       = sizeof(buffer);
    if (!usb_control_transfer(addr, 0, max_packet_size, &req, buffer)) {
        return false;
    }
    
    *status = usb_le16(buffer);
    *change = usb_le16(buffer + 2);
    return true;
}
static bool usb_hub_set_port_feature(uint8_t addr, uint8_t port, uint16_t feature, uint16_t max_packet_size)
{
    usb_device_request_t req;
    req.bmRequestType = USB_REQ_DIR_OUT | USB_REQ_TYPE_CLASS | USB_REQ_RCPT_OTHER;
    req.bRequest      = USB_REQ_SET_FEATURE;
    req.wValue        = feature;
    req.wIndex        = port;
    req.wLength       = 0;
    return usb_control_transfer(addr, 0, max_packet_size, &req, NULL);
}

static bool usb_hub_clear_port_feature(uint8_t addr, uint8_t port, uint16_t feature, uint16_t max_packet_size)
{
    usb_device_request_t req;
    req.bmRequestType = USB_REQ_DIR_OUT | USB_REQ_TYPE_CLASS | USB_REQ_RCPT_OTHER;
    req.bRequest      = USB_REQ_CLEAR_FEATURE;
    req.wValue        = feature;
    req.wIndex        = port;
    req.wLength       = 0;
    return usb_control_transfer(addr, 0, max_packet_size, &req, NULL);
}

static bool usb_hub_power_on_port(uint8_t addr, uint8_t port, uint16_t max_packet_size)
{
    if (!usb_hub_set_port_feature(addr, port, USB_FEATURE_PORT_POWER, max_packet_size)) {
        return false;
    }
    for (int i = 0; i < 50; ++i) {
        uint16_t status, change;
        if (usb_hub_get_port_status(addr, port, max_packet_size, &status, &change)) {
            if (status & USB_PORT_STATUS_POWER) {
                return true;
            }
        }
        usb_wait_ms(g_dev_hc[addr], 10);
    }
    return false;
}

static bool usb_hub_reset_port(uint8_t addr, uint8_t port, uint16_t max_packet_size)
{
    if (!usb_hub_set_port_feature(addr, port, USB_FEATURE_PORT_RESET, max_packet_size)) {
        return false;
    }

    for (int i = 0; i < 100; ++i) {
        uint16_t status, change;
        if (!usb_hub_get_port_status(addr, port, max_packet_size, &status, &change)) {
            return false;
        }
        if ((status & USB_PORT_STATUS_RESET) == 0) {
            break;
        }
        usb_wait_ms(g_dev_hc[addr], 10);
    }

    uint16_t status, change;
    if (!usb_hub_get_port_status(addr, port, max_packet_size, &status, &change)) {
        return false;
    }
    usb_hub_clear_port_feature(addr, port, USB_FEATURE_C_PORT_RESET, max_packet_size);
    usb_hub_clear_port_feature(addr, port, USB_FEATURE_C_PORT_CONNECTION, max_packet_size);
    return (status & USB_PORT_STATUS_CONNECTION) != 0;
}

static bool usb_enumerate_hub(uint8_t hub_addr, uint16_t max_packet_size, uint8_t *next_addr);

static bool usb_enumerate_device(uint8_t current_addr, usb_hc_type_t port_hc,
                                 uint16_t max_packet_size, uint8_t *next_addr)
{
    usb_device_descriptor_t desc = {0};
    bool desc_ok = false;

    for (int retry_count = 8; retry_count-- > 0;) {
        if (usb_get_device_descriptor(current_addr, &desc)) {
            desc_ok = true;
            break;
        }
        usb_wait_ms(port_hc, 50);
    }
    if (!desc_ok) {
        return false;
    }

    if (port_hc == USB_HC_XHCI) {
        uint16_t ep0_mps = usb_device_max_packet_size(desc.bMaxPacketSize0);
        xhci_evaluate_ep0_mps(current_addr, ep0_mps);
        max_packet_size = ep0_mps;
    }

    uint8_t conf_buf[256];
    usb_device_request_t req;
    req.bmRequestType = USB_REQ_DIR_IN | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE;
    req.bRequest      = USB_REQ_GET_DESCRIPTOR;
    req.wValue        = (USB_DESC_CONFIGURATION << 8) | 0;
    req.wIndex        = 0;
    req.wLength       = sizeof(conf_buf);

    bool is_hub = (desc.bDeviceClass == 9);
    uint8_t current_iface = 0xFF;

    uint8_t mass_iface = 0xFF;
    uint8_t mass_ep_in = 0;
    uint8_t mass_ep_out = 0;
    uint16_t mass_ep_in_mps = 0;
    uint16_t mass_ep_out_mps = 0;

    uint8_t kbd_iface = 0xFF;
    uint8_t kbd_ep_in = 0;
    uint16_t kbd_ep_mps = 0;

    uint8_t mouse_iface = 0xFF;
    uint8_t mouse_ep_in = 0;
    uint16_t mouse_ep_mps = 0;

    if (usb_control_transfer(current_addr, 0, max_packet_size, &req, conf_buf)) {
        uint32_t total_len = usb_le16(&conf_buf[2]);
        if (total_len > sizeof(conf_buf)) total_len = sizeof(conf_buf);

        for (uint32_t pos = 0; pos + 2 <= total_len;) {
            uint8_t len = conf_buf[pos];
            uint8_t type = conf_buf[pos + 1];

            if (len == 0 || pos + len > total_len) {
                break;
            }

            if (type == USB_DESC_INTERFACE && len >= 9) {
                current_iface = conf_buf[pos + 2];
                uint8_t iface_class = conf_buf[pos + 5];
                uint8_t iface_subclass = conf_buf[pos + 6];
                uint8_t iface_protocol = conf_buf[pos + 7];

                if (iface_class == 9) {
                    is_hub = true;
                } else if (iface_class == 0x08) {
                    mass_iface = current_iface;
                } else if (iface_class == 0x03) {
                    if (iface_protocol == 1) {
                        kbd_iface = current_iface;
                    } else if (iface_protocol == 2) {
                        mouse_iface = current_iface;
                    }
                }
            } else if (type == USB_DESC_ENDPOINT && len >= 7 && current_iface != 0xFF) {
                uint8_t ep_addr = conf_buf[pos + 2];
                uint8_t attributes = conf_buf[pos + 3];
                uint16_t mps = usb_le16(&conf_buf[pos + 4]);

                if (mass_iface != 0xFF && current_iface == mass_iface && (attributes & 0x03u) == 2) {
                    if (ep_addr & 0x80u) {
                        mass_ep_in = ep_addr & 0x7Fu;
                        mass_ep_in_mps = mps;
                    } else {
                        mass_ep_out = ep_addr & 0x7Fu;
                        mass_ep_out_mps = mps;
                    }
                } else if (kbd_iface != 0xFF && current_iface == kbd_iface && (attributes & 0x03u) == 3 && (ep_addr & 0x80u)) {
                    kbd_ep_in = ep_addr & 0x7Fu;
                    kbd_ep_mps = mps;
                } else if (mouse_iface != 0xFF && current_iface == mouse_iface && (attributes & 0x03u) == 3 && (ep_addr & 0x80u)) {
                    mouse_ep_in = ep_addr & 0x7Fu;
                    mouse_ep_mps = mps;
                }
            }

            pos += len;
        }
    }

    req.bmRequestType = USB_REQ_DIR_OUT | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE;
    req.bRequest      = USB_REQ_SET_CONFIGURATION;
    req.wValue        = conf_buf[5];
    req.wIndex        = 0;
    req.wLength       = 0;
    if (usb_control_transfer(current_addr, 0, max_packet_size, &req, NULL)) {
        if (mass_iface != 0xFF && mass_ep_in != 0 && mass_ep_out != 0) {
            g_mass_storage_addr = current_addr;
            g_mass_storage_interface = mass_iface;
            g_mass_storage_ep_in = mass_ep_in;
            g_mass_storage_ep_out = mass_ep_out;
            g_mass_storage_ep_in_mps = mass_ep_in_mps;
            g_mass_storage_ep_out_mps = mass_ep_out_mps;
        }

        if (kbd_iface != 0xFF && kbd_ep_in != 0) {
            g_hid_kbd_addr = current_addr;
            g_hid_kbd_interface = kbd_iface;
            g_hid_kbd_ep_in = kbd_ep_in;
            g_hid_kbd_ep_mps = kbd_ep_mps ? kbd_ep_mps : 8;
        }

        if (mouse_iface != 0xFF && mouse_ep_in != 0) {
            g_hid_mouse_addr = current_addr;
            g_hid_mouse_interface = mouse_iface;
            g_hid_mouse_ep_in = mouse_ep_in;
            g_hid_mouse_ep_mps = mouse_ep_mps ? mouse_ep_mps : 8;
        }
    }

    if (g_hid_kbd_addr == current_addr && g_hid_kbd_ep_in != 0) {
        usb_hid_add_keyboard(g_hid_kbd_addr, g_hid_kbd_interface, g_hid_kbd_ep_in, g_hid_kbd_ep_mps);
    }

    if (g_hid_mouse_addr == current_addr && g_hid_mouse_ep_in != 0) {
        usb_hid_add_mouse(g_hid_mouse_addr, g_hid_mouse_interface, g_hid_mouse_ep_in, g_hid_mouse_ep_mps);
    }

    if (is_hub) {
        usb_enumerate_hub(current_addr, max_packet_size, next_addr);
    }
    return true;
}

static bool usb_enumerate_hub_port(uint8_t hub_addr, uint8_t hub_port, uint16_t max_packet_size, uint8_t *next_addr)
{
    if (!usb_hub_power_on_port(hub_addr, hub_port, max_packet_size)) {
        return false;
    }

    uint16_t status, change;
    if (!usb_hub_get_port_status(hub_addr, hub_port, max_packet_size, &status, &change)) {
        return false;
    }

    if ((status & USB_PORT_STATUS_CONNECTION) == 0) {
        return false;
    }

    if (!usb_hub_reset_port(hub_addr, hub_port, max_packet_size)) {
        return false;
    }

   if (!usb_hub_get_port_status(hub_addr, hub_port, max_packet_size, &status, &change)) {
        return false;
    }
    if ((status & USB_PORT_STATUS_CONNECTION) == 0) {
        return false;
    }

    g_default_hc = g_dev_hc[hub_addr];
    g_dev_hc[0] = g_dev_hc[hub_addr];

    g_enum_parent_hub_addr = hub_addr;
    g_enum_parent_port = hub_port;

    uint8_t speed = 1;
    if (status & (1u << 9)) speed = 2;
    else if (status & (1u << 10)) speed = 3;
    g_enum_speed = speed;

    uint32_t route = g_dev_route_string[hub_addr];
    for (int i = 0; i < 20; i += 4) {
        if (((route >> i) & 0xF) == 0) {
            route |= ((hub_port & 0xF) << i);
            break;
        }
    }
    g_dev_route_string[0] = route;

    uint8_t current_addr = *next_addr;

    if (!usb_set_address(0, current_addr)) {
        g_enum_parent_hub_addr = 0;
        return false;
    }

    g_enum_parent_hub_addr = 0;

    (*next_addr)++;
    usb_wait_ms(g_dev_hc[hub_addr], 10);
    g_dev_hc[current_addr] = g_dev_hc[hub_addr];
    return usb_enumerate_device(current_addr, g_dev_hc[hub_addr], max_packet_size, next_addr);
}

static bool usb_enumerate_hub(uint8_t hub_addr, uint16_t max_packet_size, uint8_t *next_addr)
{
    usb_hub_descriptor_t hub_desc = {0};
    if (!usb_hub_get_descriptor(hub_addr, max_packet_size, &hub_desc)) {
        return false;
    }
    if (hub_desc.bNbrPorts == 0) {
        return false;
    }

    for (uint8_t port = 1; port <= hub_desc.bNbrPorts; ++port) {
        usb_hub_clear_port_feature(hub_addr, port, USB_FEATURE_C_PORT_CONNECTION, max_packet_size);
        if (usb_enumerate_hub_port(hub_addr, port, max_packet_size, next_addr)) {
        }
    }
    return true;
}

extern bool xhci_is_ready(void);

void usb_core_init(void)
{
    usb_wait_ms(g_hc_type, 20);

    uint8_t next_addr = 1;

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

    usb_hc_type_t hc_types[] = { USB_HC_XHCI, USB_HC_EHCI, USB_HC_OHCI, USB_HC_UHCI };

    for (int t = 0; t < 4; t++) {
        usb_hc_type_t cur_hc = hc_types[t];
        uint32_t num_ports = 0;

        if (cur_hc == USB_HC_XHCI && xhci_is_ready()) {
            num_ports = xhci_get_num_ports();
        } else if (cur_hc == USB_HC_EHCI && ehci_get_num_ports() > 0) {
            num_ports = ehci_get_num_ports();
        } else if (cur_hc == USB_HC_OHCI && g_ohci_ready) {
            num_ports = ohci_get_num_ports();
        } else if (cur_hc == USB_HC_UHCI && g_uhci_ready) {
            num_ports = uhci_get_num_ports();
        }

        if (num_ports == 0) {
            continue;
        }

        for (uint32_t i = 0; i < num_ports; i++) {
            bool port_valid = false;
            if      (cur_hc == USB_HC_EHCI) port_valid = ehci_port_valid(i);
            else if (cur_hc == USB_HC_OHCI) port_valid = ohci_port_valid(i);
            else if (cur_hc == USB_HC_UHCI) port_valid = uhci_port_valid(i);
            else if (cur_hc == USB_HC_XHCI) port_valid = xhci_port_valid(i);
            if (!port_valid) {
                continue;
            }

            bool port_ok = false;
            if (cur_hc == USB_HC_EHCI) {
                port_ok = ehci_reset_port(i);
            } else if (cur_hc == USB_HC_OHCI) {
                port_ok = ohci_reset_port(i);
            } else if (cur_hc == USB_HC_UHCI) {
                port_ok = uhci_reset_port(i);
            } else if (cur_hc == USB_HC_XHCI) {
                port_ok = xhci_reset_port(i);
            }

            if (!port_ok) {
                continue;
            }

            usb_wait_ms(cur_hc, 30);

            bool connected = false;
            if      (cur_hc == USB_HC_EHCI) connected = ehci_port_connected(i);
            else if (cur_hc == USB_HC_OHCI) connected = ohci_port_connected(i);
            else if (cur_hc == USB_HC_UHCI) connected = uhci_port_connected(i);
            else if (cur_hc == USB_HC_XHCI) connected = xhci_port_connected(i);
            
            if (!connected) {
                continue;
            }

            uint8_t current_addr = next_addr++;

            g_default_hc = cur_hc;
            g_dev_hc[0] = cur_hc;

            if (!usb_set_address(0, current_addr)) {
                continue;
            }

            usb_wait_ms(cur_hc, 10);
            g_dev_hc[current_addr] = cur_hc;

            uint16_t ep0_mps = 64;

            usb_device_descriptor_t desc = {0};
            if (usb_get_device_descriptor(current_addr, &desc)) {
                ep0_mps = usb_device_max_packet_size(desc.bMaxPacketSize0);

                if (cur_hc == USB_HC_XHCI) {
                    xhci_evaluate_ep0_mps(current_addr, ep0_mps);
                }
            }

            usb_enumerate_device(
                current_addr,
                cur_hc,
                ep0_mps,
                &next_addr
            );
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
        return xhci_submit_interrupt_in(addr, ep_num, max_packet_size, dma_buf, dma_phys, length);
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
        return xhci_submit_interrupt_in(addr, endpoint, max_packet_size, data, 0, length);
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
    
    usb_hid_init();
    usb_core_init();
    bot_init();
    
    if (g_hid_kbd_addr) {
        g_api->serial_write_string("USB: HID keyboard found\n");
    } else {
        g_api->serial_write_string("USB: HID keyboard NOT found\n");
    }
    if (g_hid_mouse_addr) {
        g_api->serial_write_string("USB: HID mouse found\n");
    } else {
        g_api->serial_write_string("USB: HID mouse NOT found\n");
    }
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
