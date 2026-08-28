#include "TLS13_KeySchedule.h"
#include "../HKDF/HKDF.h"
#include "../SHA256/SHA256.h"
#include <string.h>

#define LABEL_PREFIX "tls13 "

static int hkdf_expand_label(const uint8_t *secret, size_t secret_len,
                             const char *label,
                             const uint8_t *context, size_t context_len,
                             uint8_t *out, size_t out_len)
{
    /* HkdfLabel = uint16 length || "tls13 " || Label || uint8 context.length || Context */
    size_t label_len = strlen(label);
    size_t hkdf_label_len = 2 + strlen(LABEL_PREFIX) + label_len + 1 + context_len;
    uint8_t hkdf_label[hkdf_label_len];
    size_t pos = 0;

    hkdf_label[pos++] = (uint8_t)((out_len >> 8) & 0xFF);
    hkdf_label[pos++] = (uint8_t)(out_len & 0xFF);

    memcpy(hkdf_label + pos, LABEL_PREFIX, strlen(LABEL_PREFIX));
    pos += strlen(LABEL_PREFIX);

    memcpy(hkdf_label + pos, label, label_len);
    pos += label_len;

    hkdf_label[pos++] = (uint8_t)context_len;
    if (context_len > 0) {
        memcpy(hkdf_label + pos, context, context_len);
        pos += context_len;
    }

    return crypto_hkdf_expand(secret, secret_len, hkdf_label, hkdf_label_len, out, out_len);
}

int crypto_tls13_hkdf_expand_label(const uint8_t *secret, size_t secret_len,
                                   const char *label,
                                   const uint8_t *context, size_t context_len,
                                   uint8_t *out, size_t out_len)
{
    return hkdf_expand_label(secret, secret_len, label, context, context_len, out, out_len);
}

int crypto_tls13_derive_early_secret(const uint8_t *psk, size_t psk_len,
                                     uint8_t early_secret[32])
{
    uint8_t salt[32];
    memset(salt, 0, 32);

    if (psk == NULL || psk_len == 0) {
        uint8_t zero_ikm[32] = {0};
        return crypto_hkdf_extract(salt, 32, zero_ikm, 32, early_secret);
    }

    return crypto_hkdf_extract(salt, 32, psk, psk_len, early_secret);
}

int crypto_tls13_derive_handshake_secret(const uint8_t early_secret[32],
                                         const uint8_t *shared_secret, size_t shared_secret_len,
                                         uint8_t handshake_secret[32])
{
    uint8_t empty_hash[32];
    crypto_sha256(NULL, 0, empty_hash);

    uint8_t derived_secret[32];
    if (hkdf_expand_label(early_secret, 32, "derived", empty_hash, 32, derived_secret, 32) < 0) {
        return -1;
    }

    return crypto_hkdf_extract(derived_secret, 32, shared_secret, shared_secret_len, handshake_secret);
}

int crypto_tls13_derive_master_secret(const uint8_t handshake_secret[32],
                                      uint8_t master_secret[32])
{
    uint8_t empty_hash[32];
    crypto_sha256(NULL, 0, empty_hash);

    uint8_t derived_secret[32];
    if (hkdf_expand_label(handshake_secret, 32, "derived", empty_hash, 32, derived_secret, 32) < 0) {
        return -1;
    }

    uint8_t empty_ikm[32] = {0};
    return crypto_hkdf_extract(derived_secret, 32, empty_ikm, 32, master_secret);
}

int crypto_tls13_derive_traffic_keys(const uint8_t *secret, size_t secret_len,
                                     const char *label,
                                     uint8_t *key, size_t key_len,
                                     uint8_t *iv, size_t iv_len)
{
    uint8_t traffic_secret[32];
    uint8_t empty_hash[32];
    crypto_sha256(NULL, 0, empty_hash);

    if (hkdf_expand_label(secret, secret_len, label, empty_hash, 32, traffic_secret, 32) < 0) {
        return -1;
    }

    if (key && key_len > 0) {
        if (hkdf_expand_label(traffic_secret, 32, "key", NULL, 0, key, key_len) < 0) {
            return -1;
        }
    }

    if (iv && iv_len > 0) {
        if (hkdf_expand_label(traffic_secret, 32, "iv", NULL, 0, iv, iv_len) < 0) {
            return -1;
        }
    }

    return 0;
}
