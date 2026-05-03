#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Network.h"
#include "networkTest.h"
#include "../../../../libc/include/string.h"

static void try_resolve(const char *host)
{
    uint32_t ip = dns_resolve(host);
    if (ip == 0u) {
        return;
    }
}

void networkTest_main(void)
{
    const char *message = "Hello, Network!";
    uint16_t len = (uint16_t)strlen(message);
    bool success = udp_send(0x0A000202u, 12345, 12346, message, len);
    success = udp_send(0xFFFFFFFFu, 12345, 12346, message, len);

    try_resolve("example.com");
    try_resolve("www.google.com");
}

void _start(void)
{
    networkTest_main();
    while (1) {
        process_yield();
    }
}
