#include "NetworkBuiltinDrivers.h"
#include "DriverManager.h"
#include "kernel/interfaces/device.h"

#include "Network/ethernet/Ethernet.h"
#include "Network/arp/ARP.h"
#include "Network/ipv4.h"
#include "Network/icmp/ICMP.h"
#include "Network/udp/UDP.h"
#include "Network/tcp/TCP.h"
#include "Network/dhcp/DHCP.h"

/*
 * See NetworkBuiltinDrivers.h for the hybrid design this implements
 * (Docs/Others/TODO_OS_Refactor.md phase P5, 9.2). Each layer gets its own
 * vtable type (matching this codebase's existing convention of one
 * `*_driver_t` struct per driver rather than a forced-generic shape --
 * compare fat32_driver_t/iso9660_driver_t/exfat_driver_t) populated with
 * that layer's *actual* public entry points, so a lookup through
 * DeviceRegistry returns something genuinely callable, not just a
 * name/description pair.
 *
 * deps[] on each entry is real, accurate dependency information (derived
 * from grepping Kernel/Network's actual #include graph, not guessed), even
 * though nothing currently walks it programmatically -- driver_module_
 * manager_init()'s dependency-ordering logic (Kernel/Drivers/Module/
 * DriverModule.c) is designed for loadable .ELF modules going through
 * driver_module_init(), which these are not; here the correct order is
 * simply the fixed call order network_stack_init() already uses
 * (Kernel/Network/network_main.c: Ethernet -> ARP -> IPv4 -> {ICMP, UDP,
 * TCP} -> DHCP). Recording deps[] here documents that order machine-
 * readably for whatever eventually consumes it (a /proc/bus/devices
 * listing, or a future genuine ELF-module conversion).
 */

typedef struct {
    bool     (*init)(void);
    bool     (*is_ready)(void);
    bool     (*send)(uint16_t ether_type, const uint8_t dst_mac[ETHERNET_ADDR_LEN],
                     const void *payload, uint16_t payload_len);
    bool     (*register_handler)(uint16_t ether_type, ethernet_type_handler_t handler);
    const char *deps[4];
} ethernet_builtin_driver_t;

static bool ethernet_init_void(void) { return ethernet_init(); }

static const ethernet_builtin_driver_t g_ethernet_driver = {
    .init = ethernet_init_void,
    .is_ready = ethernet_is_ready,
    .send = ethernet_send,
    .register_handler = ethernet_register_handler,
    .deps = { NULL },
};

typedef struct {
    bool     (*lookup)(uint32_t ipv4_addr, uint8_t mac_out[6]);
    bool     (*resolve)(uint32_t ipv4_addr, uint8_t mac_out[6], uint32_t timeout_ms);
    bool     (*send_request)(uint32_t target_ipv4_addr);
    const char *deps[4];
} arp_builtin_driver_t;

static const arp_builtin_driver_t g_arp_driver = {
    .lookup = arp_lookup,
    .resolve = arp_resolve,
    .send_request = arp_send_request,
    .deps = { "Ethernet_Driver", NULL },
};

typedef struct {
    uint32_t (*local_address)(void);
    bool     (*send)(uint32_t dst_ipv4_addr, uint8_t protocol,
                     const void *payload, uint16_t payload_len);
    bool     (*register_protocol)(uint8_t protocol, ipv4_protocol_handler_t handler);
    const char *deps[4];
} ipv4_builtin_driver_t;

static const ipv4_builtin_driver_t g_ipv4_driver = {
    .local_address = ipv4_local_address,
    .send = ipv4_send,
    .register_protocol = ipv4_register_protocol,
    .deps = { "Ethernet_Driver", "ARP_Driver", NULL },
};

typedef struct {
    bool (*send_echo_request)(uint32_t dst_ip, uint16_t id, uint16_t seq,
                              const void *data, uint16_t data_len);
    const char *deps[4];
} icmp_builtin_driver_t;

static const icmp_builtin_driver_t g_icmp_driver = {
    .send_echo_request = icmp_send_echo_request,
    .deps = { "IPv4_Driver", NULL },
};

typedef struct {
    bool    (*bind)(uint16_t port, udp_rx_handler_t handler);
    void    (*unbind)(uint16_t port);
    bool    (*send)(uint32_t dst_ipv4_addr, uint16_t src_port, uint16_t dst_port,
                    const void *payload, uint16_t payload_len);
    const char *deps[4];
} udp_builtin_driver_t;

static const udp_builtin_driver_t g_udp_driver = {
    .bind = udp_bind,
    .unbind = udp_unbind,
    .send = udp_send,
    .deps = { "IPv4_Driver", NULL },
};

typedef struct {
    int32_t  (*connect)(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port);
    int32_t  (*listen)(uint16_t port);
    int32_t  (*send)(int32_t conn_id, const void *data, uint16_t len);
    int32_t  (*recv)(int32_t conn_id, void *buf, uint16_t buf_len);
    int32_t  (*close)(int32_t conn_id);
    const char *deps[4];
} tcp_builtin_driver_t;

static const tcp_builtin_driver_t g_tcp_driver = {
    .connect = tcp_connect,
    .listen = tcp_listen,
    .send = tcp_send,
    .recv = tcp_recv,
    .close = tcp_close,
    .deps = { "IPv4_Driver", NULL },
};

typedef struct {
    bool     (*discover)(void);
    uint32_t (*get_assigned_ip)(void);
    uint32_t (*get_gateway)(void);
    uint32_t (*get_subnet_mask)(void);
    const char *deps[4];
} dhcp_builtin_driver_t;

static const dhcp_builtin_driver_t g_dhcp_driver = {
    .discover = dhcp_discover,
    .get_assigned_ip = dhcp_get_assigned_ip,
    .get_gateway = dhcp_get_gateway,
    .get_subnet_mask = dhcp_get_subnet_mask,
    .deps = { "IPv4_Driver", "UDP_Driver", NULL },
};

void network_builtin_drivers_register(void)
{
    (void)driver_manager_attach("Ethernet_Driver", DEVICE_TYPE_NET_PROTOCOL, &g_ethernet_driver);
    (void)driver_manager_attach("ARP_Driver", DEVICE_TYPE_NET_PROTOCOL, &g_arp_driver);
    (void)driver_manager_attach("IPv4_Driver", DEVICE_TYPE_NET_PROTOCOL, &g_ipv4_driver);
    (void)driver_manager_attach("ICMP_Driver", DEVICE_TYPE_NET_PROTOCOL, &g_icmp_driver);
    (void)driver_manager_attach("UDP_Driver", DEVICE_TYPE_NET_PROTOCOL, &g_udp_driver);
    (void)driver_manager_attach("TCP_Driver", DEVICE_TYPE_NET_PROTOCOL, &g_tcp_driver);
    (void)driver_manager_attach("DHCP_Driver", DEVICE_TYPE_NET_PROTOCOL, &g_dhcp_driver);
}
