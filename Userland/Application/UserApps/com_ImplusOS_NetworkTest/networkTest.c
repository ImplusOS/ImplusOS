#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Network.h"
#include "networkTest.h"
#include "../../../../libc/include/string.h"

static void print_ipv4(uint32_t ip)
{
    serial_write_uint16((uint16_t)((ip >> 24) & 0xFFu));
    serial_write_string(".");
    serial_write_uint16((uint16_t)((ip >> 16) & 0xFFu));
    serial_write_string(".");
    serial_write_uint16((uint16_t)((ip >> 8) & 0xFFu));
    serial_write_string(".");
    serial_write_uint16((uint16_t)(ip & 0xFFu));
}

static void try_resolve(const char *host)
{
    serial_write_string("[NetworkTest] userland dns_resolve(");
    serial_write_string(host);
    serial_write_string(") ...\n");

    uint32_t ip = dns_resolve(host);
    if (ip == 0u) {
        serial_write_string("[NetworkTest]   -> FAILED (server unreachable or NXDOMAIN)\n");
        return;
    }

    serial_write_string("[NetworkTest]   -> ");
    print_ipv4(ip);
    serial_write_string("\n");
}

void networkTest_main(void)
{
    serial_write_string("[NetworkTest] Starting userland network smoke test.\n");

    const char *message = "Hello, Network!";
    uint16_t len = (uint16_t)strlen(message);
    bool success = udp_send(0x0A000202u, 12345, 12346, message, len);
    serial_write_string(success
                        ? "[NetworkTest] UDP unicast to 10.0.2.2:12346 sent.\n"
                        : "[NetworkTest] UDP unicast send failed.\n");
    success = udp_send(0xFFFFFFFFu, 12345, 12346, message, len);
    serial_write_string(success
                        ? "[NetworkTest] UDP broadcast sent.\n"
                        : "[NetworkTest] UDP broadcast send failed.\n");

    try_resolve("example.com");
    try_resolve("www.google.com");

    serial_write_string("[NetworkTest] Done.\n");
}

void _start(void)
{
    networkTest_main();
    while (1) {
        process_yield();
    }
}
