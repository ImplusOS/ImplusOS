#pragma once

#include <stdint.h>

/*
 * Userland DNS resolver. Runs entirely in the caller's address space and
 * uses the UDP syscalls (udp_send / udp_bind_port / udp_recv) to talk to
 * the remote DNS server.
 *
 * Returns the resolved IPv4 address in host byte order, or 0 on error.
 */
uint32_t dns_resolve(const char *hostname);
uint32_t dns_resolve_with_server(const char *hostname, uint32_t dns_server_ip);

/*
 * Override the default DNS server used by dns_resolve(). Takes effect for
 * subsequent calls. Passing 0 restores the default (10.0.2.3, QEMU slirp).
 */
void dns_set_default_server(uint32_t dns_server_ip);
