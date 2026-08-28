#include "X509_Verify.h"
#include "../X509/X509.h"
#include "../ASN1/ASN1.h"
#include "../SHA256/SHA256.h"
#include "../SHA384/SHA384.h"
#include "../SHA512/SHA512.h"
#include "../ECDSA/ECDSA.h"
#include "../RSA_PKCS1_V15/RSA_PKCS1_V15.h"
#include <string.h>

int x509_verify_signature(const uint8_t *data, size_t data_len,
                          const uint8_t *signature, size_t sig_len,
                          const uint8_t *public_key, size_t pubkey_len,
                          int key_type)
{
    if (data == NULL || signature == NULL || public_key == NULL) return -1;

    if (key_type == 1) {
        /* EC key - use ECDSA verify */
        if (pubkey_len == 65) {
            /* Uncompressed EC point, skip 0x04 prefix */
            uint8_t raw_key[64];
            memcpy(raw_key, public_key + 1, 64);

            uint8_t hash[32];
            crypto_sha256(data, data_len, hash);

            return crypto_ecdsa_verify_der(raw_key, hash, signature, sig_len);
        }
        return -1;
    }

    /* RSA key */
    uint8_t hash[32];
    crypto_sha256(data, data_len, hash);

    return crypto_rsa_pkcs1_v15_verify(public_key, pubkey_len,
                                       NULL, 0,
                                       hash, 32,
                                       signature, sig_len,
                                       RSA_HASH_NID_SHA256);
}

int x509_verify_certificate_chain(const uint8_t * const *certs_der, const size_t *certs_len,
                                  size_t cert_count,
                                  const uint8_t *root_der, size_t root_len)
{
    if (certs_der == NULL || certs_len == NULL) return -1;
    if (root_der == NULL) return -1;

    x509_cert_t root_cert;
    if (x509_parse_certificate(root_der, root_len, &root_cert) < 0) return -1;

    x509_cert_t prev_cert = root_cert;

    for (size_t i = 0; i < cert_count; ++i) {
        x509_cert_t cert;
        if (x509_parse_certificate(certs_der[i], certs_len[i], &cert) < 0) return -1;

        /* Check issuer matches previous cert's subject */
        if (strcmp(cert.issuer, prev_cert.subject) != 0) return -1;

        /* Verify signature on this cert using previous cert's public key */
        uint8_t tbs_data[2048];
        size_t tbs_len = 0;

        asn1_node_t outer[4];
        size_t outer_count = 4;
        if (asn1_decode_sequence(certs_der[i], certs_len[i], outer, &outer_count) < 0) return -1;
        if (outer_count < 1) return -1;

        /* TBS data is outer[0].data with length outer[0].length */
        tbs_len = outer[0].length;
        if (tbs_len > sizeof(tbs_data)) return -1;
        memcpy(tbs_data, outer[0].data, tbs_len);

        uint8_t hash[32];
        crypto_sha256(tbs_data, tbs_len, hash);

        int ret = -1;

        if (prev_cert.key_type == X509_KEY_RSA) {
            ret = crypto_rsa_pkcs1_v15_verify(
                prev_cert.modulus, prev_cert.modulus_len,
                prev_cert.exponent, prev_cert.exponent_len,
                hash, 32,
                cert.signature, cert.signature_len,
                RSA_HASH_NID_SHA256);
        } else if (prev_cert.key_type == X509_KEY_EC) {
            uint8_t raw_pubkey[64];
            if (prev_cert.ec_point_len == 65) {
                memcpy(raw_pubkey, prev_cert.ec_point + 1, 64);
            } else if (prev_cert.ec_point_len == 64) {
                memcpy(raw_pubkey, prev_cert.ec_point, 64);
            } else {
                return -1;
            }
            ret = crypto_ecdsa_verify_der(raw_pubkey, hash, cert.signature, cert.signature_len);
        }

        if (ret < 0) return -1;

        prev_cert = cert;
    }

    return 0;
}
