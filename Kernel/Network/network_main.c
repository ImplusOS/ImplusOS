#include "network_main.h"
#include "network_utils.h"

#include "kernel/config.h"
#include "Drivers/Module/DriverManager.h"
#include "Network/ethernet/Ethernet.h"
#include "Network/arp/ARP.h"
#include "ipv4.h"
#include "Network/udp/UDP.h"
#include "Network/tcp/TCP.h"
#include "Network/icmp/ICMP.h"
#include "Network/dhcp/DHCP.h"

#ifndef OS_CONFIG_NET_IPV4_ADDR
#define OS_CONFIG_NET_IPV4_ADDR 0x0A00020FUL
#endif

#ifndef OS_CONFIG_NET_IPV4_MASK
#define OS_CONFIG_NET_IPV4_MASK 0xFFFFFF00UL
#endif

#ifndef OS_CONFIG_NET_IPV4_GATEWAY
#define OS_CONFIG_NET_IPV4_GATEWAY 0x0A000202UL
#endif

static int g_network_ready = 0;
static uint32_t g_timer_ticks_for_aging = 0u;

bool network_stack_init(void)
{
    if (g_network_ready != 0) {
        return true;
    }

    if (!ethernet_init()) {
        return false;
    }

    uint32_t local_ip = (uint32_t)OS_CONFIG_NET_IPV4_ADDR;
    uint32_t netmask = (uint32_t)OS_CONFIG_NET_IPV4_MASK;
    uint32_t gateway = (uint32_t)OS_CONFIG_NET_IPV4_GATEWAY;

    arp_init(local_ip, netmask, gateway);
    ipv4_init(local_ip, netmask, gateway);
    udp_init();
    tcp_init();
    icmp_init();
    dhcp_init();

    g_timer_ticks_for_aging = 0u;
    g_network_ready = 1;
    return true;
}

bool network_stack_is_ready(void)
{
    return g_network_ready != 0;
}

void network_stack_poll(void)
{
    if (g_network_ready == 0) {
        return;
    }

    ethernet_poll();
}

void network_stack_schedule_poll(void)
{
    if (g_network_ready == 0) {
        return;
    }
    driver_manager_nic_schedule_poll();
}

bool network_stack_check_poll(void)
{
    if (g_network_ready == 0) {
        return false;
    }
    return driver_manager_nic_check_poll();
}

void network_stack_on_timer_tick(void)
{
    if (g_network_ready == 0) {
        return;
    }

    driver_manager_nic_schedule_poll();

    g_timer_ticks_for_aging++;
    if (g_timer_ticks_for_aging >= 60u) {
        g_timer_ticks_for_aging = 0u;
        ipv4_process_timer();
        tcp_process_timer();
    }
}

uint32_t network_stack_local_ipv4(void)
{
    return ipv4_local_address();
}
