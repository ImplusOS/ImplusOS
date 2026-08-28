#pragma once

/*
 * Registers each network protocol layer (Ethernet/ARP/IPv4/ICMP/UDP/TCP/
 * DHCP) as an individually named DEVICE_TYPE_NET_PROTOCOL entry in
 * DeviceRegistry, with a real callable vtable and recorded dependency
 * metadata -- see Docs/Others/TODO_OS_Refactor.md phase P5, 9.2, and the
 * per-layer vtable typedefs in NetworkBuiltinDrivers.c.
 *
 * Scope note (the hybrid approach decided for 9.2): this does NOT convert
 * Kernel/Network's internal cross-layer calls (e.g. ARP.c calling
 * ethernet_send() directly, DHCP.c calling ipv4_send()/udp_send()
 * directly) to go through these vtables via driver_manager_find() --
 * those stay exactly as direct C function calls, unchanged. What this
 * *does* provide: (1) each layer is visible in driver listings under its
 * own name, the same way a real loaded .ELF module is; (2) each layer's
 * public API is available as a genuine function-pointer vtable a new,
 * decoupled caller (e.g. a userland-facing syscall path added later, or a
 * future incremental "switch one layer's callers to the vtable" pass) can
 * reach via driver_manager_find(DEVICE_TYPE_NET_PROTOCOL, "..._Driver")
 * instead of an extern declaration; (3) the deps[] each entry records is
 * accurate, real dependency information a future full ELF-module
 * conversion (should the project pursue one) can use as-is.
 */
void network_builtin_drivers_register(void);
