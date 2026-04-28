#include "DNS.h"

#include <stddef.h>
#include <stdint.h>

#include "../../API/Network.h"
#include "../../API/Process.h"
#include "../../API/Serial.h"
#include "../../../libc/include/string.h"

#define DNS_REMOTE_PORT 53u
#define DNS_LOCAL_PORT  10053u

#define DNS_QTYPE_A     1u
#define DNS_QCLASS_IN   1u

#define DNS_MAX_QUERY_BYTES 512u
#define DNS_MAX_RECV_BYTES  (8u + DNS_MAX_QUERY_BYTES)

static uint32_t g_default_dns_server = 0x0A000203u;

static uint16_t dns_encode_name(uint8_t *out, uint16_t out_max, const char *name)
{
    uint16_t pos = 0u;
    while (*name != '\0') {
        const char *dot = name;
        while (*dot != '\0' && *dot != '.') {
            dot++;
        }
        uint16_t label_len = (uint16_t)(dot - name);
        if (label_len == 0u || label_len > 63u) {
            return 0u;
        }
        if ((uint32_t)pos + 1u + label_len + 1u > out_max) {
            return 0u;
        }
        out[pos++] = (uint8_t)label_len;
        for (uint16_t i = 0; i < label_len; ++i) {
            out[pos++] = (uint8_t)name[i];
        }
        name = (*dot == '.') ? dot + 1 : dot;
    }
    if (pos + 1u > out_max) {
        return 0u;
    }
    out[pos++] = 0u;
    return pos;
}

static uint32_t dns_parse_dotted_ipv4(const char *str)
{
    uint32_t ip = 0u;
    int parts = 0;
    const char *p = str;
    while (*p != '\0' && parts < 4) {
        uint32_t val = 0u;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10u + (uint32_t)(*p - '0');
            p++;
            digits++;
            if (val > 255u) {
                return 0u;
            }
        }
        if (digits == 0) {
            return 0u;
        }
        ip = (ip << 8) | (val & 0xFFu);
        parts++;
        if (*p == '.') {
            p++;
        } else if (*p != '\0') {
            return 0u;
        }
    }
    if (parts != 4 || *p != '\0') {
        return 0u;
    }
    return ip;
}

static uint16_t dns_rand_txid(void)
{
    static uint16_t counter = 0x4A15u;
    counter = (uint16_t)(counter * 0x9E37u + 0x7B4Du);
    return counter;
}

void dns_set_default_server(uint32_t dns_server_ip)
{
    g_default_dns_server = (dns_server_ip != 0u) ? dns_server_ip : 0x0A000203u;
}

uint32_t dns_resolve(const char *hostname)
{
    return dns_resolve_with_server(hostname, g_default_dns_server);
}

uint32_t dns_resolve_with_server(const char *hostname, uint32_t dns_server_ip)
{
    if (hostname == NULL || hostname[0] == '\0' || dns_server_ip == 0u) {
        return 0u;
    }

    uint32_t dotted = dns_parse_dotted_ipv4(hostname);
    if (dotted != 0u) {
        return dotted;
    }

    uint8_t query[DNS_MAX_QUERY_BYTES];
    memset(query, 0, sizeof(query));

    uint16_t txid = dns_rand_txid();

    query[0] = (uint8_t)((txid >> 8) & 0xFFu);
    query[1] = (uint8_t)(txid & 0xFFu);

    query[2] = 0x01u;
    query[3] = 0x00u;

    query[4] = 0x00u;
    query[5] = 0x01u;

    uint16_t offset = 12u;
    uint16_t name_len = dns_encode_name(query + offset,
                                        DNS_MAX_QUERY_BYTES - offset - 4u,
                                        hostname);
    if (name_len == 0u) {
        return 0u;
    }
    offset = (uint16_t)(offset + name_len);

    query[offset++] = 0u;
    query[offset++] = (uint8_t)DNS_QTYPE_A;
    query[offset++] = 0u;
    query[offset++] = (uint8_t)DNS_QCLASS_IN;

    if (udp_bind_port(DNS_LOCAL_PORT) < 0) {
        return 0u;
    }

    uint32_t resolved_ip = 0u;
    const uint32_t k_retries = 3u;
    const uint32_t k_poll_iters = 200u;

    for (uint32_t attempt = 0u; attempt < k_retries && resolved_ip == 0u; ++attempt) {
        if (!udp_send(dns_server_ip,
                      DNS_LOCAL_PORT,
                      DNS_REMOTE_PORT,
                      query,
                      offset)) {
            continue;
        }

        for (uint32_t i = 0u; i < k_poll_iters && resolved_ip == 0u; ++i) {
            uint8_t buf[DNS_MAX_RECV_BYTES];
            int32_t got = udp_recv(DNS_LOCAL_PORT, buf, sizeof(buf));
            if (got <= 8) {
                process_yield();
                continue;
            }

            uint16_t payload_len = (uint16_t)((uint16_t)buf[6] | ((uint16_t)buf[7] << 8));
            if ((uint32_t)payload_len + 8u > (uint32_t)got) {
                continue;
            }
            if (payload_len < 12u) {
                continue;
            }

            const uint8_t *pkt = buf + 8u;
            uint16_t id = (uint16_t)(((uint16_t)pkt[0] << 8) | (uint16_t)pkt[1]);
            uint16_t flags = (uint16_t)(((uint16_t)pkt[2] << 8) | (uint16_t)pkt[3]);
            uint16_t qd = (uint16_t)(((uint16_t)pkt[4] << 8) | (uint16_t)pkt[5]);
            uint16_t an = (uint16_t)(((uint16_t)pkt[6] << 8) | (uint16_t)pkt[7]);

            if (id != txid) {
                continue;
            }
            if ((flags & 0x8000u) == 0u) {
                continue;
            }
            if ((flags & 0x000Fu) != 0u) {
                break;
            }
            if (an == 0u) {
                break;
            }

            uint16_t p = 12u;
            for (uint16_t q = 0u; q < qd && p < payload_len; ++q) {
                while (p < payload_len && pkt[p] != 0u) {
                    if ((pkt[p] & 0xC0u) == 0xC0u) {
                        p = (uint16_t)(p + 2u);
                        goto q_done;
                    }
                    p = (uint16_t)(p + 1u + pkt[p]);
                }
                p = (uint16_t)(p + 1u);
            q_done:
                p = (uint16_t)(p + 4u);
            }

            for (uint16_t a = 0u; a < an && (uint32_t)p + 10u <= payload_len; ++a) {
                if ((pkt[p] & 0xC0u) == 0xC0u) {
                    p = (uint16_t)(p + 2u);
                } else {
                    while (p < payload_len && pkt[p] != 0u) {
                        p = (uint16_t)(p + 1u + pkt[p]);
                    }
                    p = (uint16_t)(p + 1u);
                }
                if ((uint32_t)p + 10u > payload_len) {
                    break;
                }
                uint16_t rtype = (uint16_t)(((uint16_t)pkt[p] << 8) | (uint16_t)pkt[p + 1]);
                uint16_t rdlen = (uint16_t)(((uint16_t)pkt[p + 8] << 8) | (uint16_t)pkt[p + 9]);
                p = (uint16_t)(p + 10u);
                if ((uint32_t)p + rdlen > payload_len) {
                    break;
                }
                if (rtype == DNS_QTYPE_A && rdlen == 4u) {
                    resolved_ip = ((uint32_t)pkt[p] << 24) |
                                  ((uint32_t)pkt[p + 1] << 16) |
                                  ((uint32_t)pkt[p + 2] << 8) |
                                  (uint32_t)pkt[p + 3];
                    break;
                }
                p = (uint16_t)(p + rdlen);
            }
        }
    }

    udp_unbind_port(DNS_LOCAL_PORT);
    return resolved_ip;
}
