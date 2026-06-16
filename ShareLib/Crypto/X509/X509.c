#include "X509.h"
#include "../ASN1/ASN1.h"
#include "../Base64/Base64.h"
#include "../SHA256/SHA256.h"
#include <string.h>

static int parse_algorithm_identifier(const uint8_t *buf, size_t len)
{
    asn1_node_t seq_nodes[3];
    size_t count = 3;
    if (asn1_decode_sequence(buf, len, seq_nodes, &count) < 0) return -1;
    if (count < 1) return -1;

    asn1_node_t oid_node;
    if (asn1_decode(seq_nodes[0].data, seq_nodes[0].length, &oid_node) < 0) return -1;

    /* Check OID for known algorithms */
    if (oid_node.length == 8 && oid_node.data[0] == 0x2a &&
        oid_node.data[1] == 0x86 && oid_node.data[2] == 0x48 &&
        oid_node.data[3] == 0x86 && oid_node.data[4] == 0xf7 &&
        oid_node.data[5] == 0x0d && oid_node.data[6] == 0x01 &&
        oid_node.data[7] == 0x01) {
        /* 1.2.840.113549.1.1.x = RSA */
        if (oid_node.length >= 9) {
            if (oid_node.data[8] == 0x01) return 0;  /* rsaEncryption */
            if (oid_node.data[8] == 0x0b) return 11; /* sha256WithRSAEncryption */
        }
    }
    /* ECDSA: 1.2.840.10045.2.1 = ecPublicKey */
    if (oid_node.length == 7 &&
        oid_node.data[0] == 0x2a && oid_node.data[1] == 0x86 &&
        oid_node.data[2] == 0x48 && oid_node.data[3] == 0xce &&
        oid_node.data[4] == 0x3d && oid_node.data[5] == 0x02 &&
        oid_node.data[6] == 0x01) {
        return 1; /* EC */
    }
    /* ecdsa-with-SHA256: 1.2.840.10045.4.3.2 */
    if (oid_node.length == 8 &&
        oid_node.data[0] == 0x2a && oid_node.data[1] == 0x86 &&
        oid_node.data[2] == 0x48 && oid_node.data[3] == 0xce &&
        oid_node.data[4] == 0x3d && oid_node.data[5] == 0x04 &&
        oid_node.data[6] == 0x03 && oid_node.data[7] == 0x02) {
        return 12; /* ecdsa-with-SHA256 */
    }

    return -1;
}

static int parse_name(const uint8_t *buf, size_t len, char *out, size_t out_size)
{
    asn1_node_t rdn_nodes[32];
    size_t rdn_count = 32;

    if (asn1_decode_sequence(buf, len, rdn_nodes, &rdn_count) < 0) {
        return -1;
    }

    out[0] = '\0';
    size_t pos = 0;
    int first = 1;

    for (size_t i = 0; i < rdn_count; ++i) {
        asn1_node_t attr_nodes[16];
        size_t attr_count = 16;
        if (asn1_decode_sequence(rdn_nodes[i].data, rdn_nodes[i].length, attr_nodes, &attr_count) < 0) {
            continue;
        }

        for (size_t j = 0; j < attr_count; ++j) {
            asn1_node_t sub_nodes[8];
            size_t sub_count = 8;
            if (asn1_decode_sequence(attr_nodes[j].data, attr_nodes[j].length, sub_nodes, &sub_count) < 0)
                continue;
            if (sub_count < 2) continue;

            /* Skip OID, just get the string value */
            asn1_node_t val;
            if (asn1_decode(sub_nodes[1].data, sub_nodes[1].length, &val) < 0)
                continue;

            if (!first && pos < out_size - 1) {
                out[pos++] = ',';
            }
            first = 0;

            size_t to_copy = val.length;
            if (pos + to_copy >= out_size) {
                to_copy = out_size - pos - 1;
            }
            memcpy(out + pos, val.data, to_copy);
            pos += to_copy;
        }
    }
    out[pos] = '\0';
    return 0;
}

static int parse_validity(const uint8_t *buf, size_t len, x509_time_t *not_before, x509_time_t *not_after)
{
    asn1_node_t nodes[4];
    size_t count = 4;
    if (asn1_decode_sequence(buf, len, nodes, &count) < 0 || count < 2) return -1;

    /* Parse time strings */
    for (int idx = 0; idx < 2; ++idx) {
        x509_time_t *t = (idx == 0) ? not_before : not_after;
        memset(t, 0, sizeof(x509_time_t));

        asn1_node_t time_node;
        if (asn1_decode(nodes[idx].data, nodes[idx].length, &time_node) < 0) return -1;

        char time_str[20];
        size_t tl = time_node.length;
        if (tl > sizeof(time_str) - 1) tl = sizeof(time_str) - 1;
        memcpy(time_str, time_node.data, tl);
        time_str[tl] = '\0';

        if (time_node.tag == 0x17) {
            /* UTCTime: YYMMDDHHMMSSZ or YYMMDDHHMMZ */
            if (tl >= 10) {
                int yy = (time_str[0] - '0') * 10 + (time_str[1] - '0');
                t->year = (yy >= 50) ? 1900 + yy : 2000 + yy;
                t->month = (time_str[2] - '0') * 10 + (time_str[3] - '0');
                t->day = (time_str[4] - '0') * 10 + (time_str[5] - '0');
                t->hour = (time_str[6] - '0') * 10 + (time_str[7] - '0');
                t->minute = (time_str[8] - '0') * 10 + (time_str[9] - '0');
                if (tl >= 12) {
                    t->second = (time_str[10] - '0') * 10 + (time_str[11] - '0');
                }
            }
        } else if (time_node.tag == 0x18) {
            /* GeneralizedTime: YYYYMMDDHHMMSSZ */
            if (tl >= 12) {
                t->year = (time_str[0] - '0') * 1000 + (time_str[1] - '0') * 100 +
                          (time_str[2] - '0') * 10 + (time_str[3] - '0');
                t->month = (time_str[4] - '0') * 10 + (time_str[5] - '0');
                t->day = (time_str[6] - '0') * 10 + (time_str[7] - '0');
                t->hour = (time_str[8] - '0') * 10 + (time_str[9] - '0');
                t->minute = (time_str[10] - '0') * 10 + (time_str[11] - '0');
                if (tl >= 14) {
                    t->second = (time_str[12] - '0') * 10 + (time_str[13] - '0');
                }
            }
        }
    }
    return 0;
}

int x509_parse_certificate(const uint8_t *der, size_t len, x509_cert_t *cert)
{
    if (der == NULL || cert == NULL) return -1;
    memset(cert, 0, sizeof(x509_cert_t));

    /* Parse outer SEQUENCE */
    asn1_node_t outer_nodes[4];
    size_t outer_count = 4;
    if (asn1_decode_sequence(der, len, outer_nodes, &outer_count) < 0 || outer_count < 3) {
        return -1;
    }

    /* outer_nodes[0] = TBSCertificate (SEQUENCE)
       outer_nodes[1] = AlgorithmIdentifier (SEQUENCE)
       outer_nodes[2] = signatureValue (BIT STRING) */

    /* Parse signature value */
    {
        asn1_node_t sig_node;
        if (asn1_decode(outer_nodes[2].data, outer_nodes[2].length, &sig_node) < 0)
            return -1;
        if ((sig_node.tag & 0x1F) != ASN1_TAG_BIT_STRING) return -1;
        /* Skip the unused bits byte */
        const uint8_t *sig_start = sig_node.data;
        size_t sig_skip = 1;
        cert->signature_len = sig_node.length - sig_skip;
        if (cert->signature_len > sizeof(cert->signature))
            cert->signature_len = sizeof(cert->signature);
        memcpy(cert->signature, sig_start + sig_skip, cert->signature_len);
    }

    /* Parse signature algorithm */
    {
        cert->sig_algorithm = parse_algorithm_identifier(
            outer_nodes[1].data, outer_nodes[1].length);
    }

    /* Parse TBSCertificate */
    asn1_node_t tbs_nodes[20];
    size_t tbs_count = 20;
    if (asn1_decode_sequence(outer_nodes[0].data, outer_nodes[0].length, tbs_nodes, &tbs_count) < 0) {
        return -1;
    }

    /* tbs_nodes[0] = version [0] (optional, context-specific, constructed)
       tbs_nodes[1] = serialNumber (INTEGER)
       tbs_nodes[2] = signature (AlgorithmIdentifier)
       tbs_nodes[3] = issuer (Name)
       tbs_nodes[4] = validity (Validity)
       tbs_nodes[5] = subject (Name)
       tbs_nodes[6] = subjectPublicKeyInfo (SubjectPublicKeyInfo) */

    int tbs_idx = 0;

    /* Check for version [0] (context-specific tag 0x00 after masking) */
    if (tbs_count > 0 && tbs_nodes[0].tag == 0x00) {
        ++tbs_idx;
    }

    /* Serial number */
    if (tbs_idx < (int)tbs_count) {
        asn1_node_t serial_node;
        if (asn1_decode(tbs_nodes[tbs_idx].data, tbs_nodes[tbs_idx].length, &serial_node) >= 0) {
            cert->serial_len = serial_node.length;
            if (cert->serial_len > sizeof(cert->serial))
                cert->serial_len = sizeof(cert->serial);
            memcpy(cert->serial, serial_node.data, cert->serial_len);
        }
        ++tbs_idx;
    }

    /* Signature algorithm in TBS */
    if (tbs_idx < (int)tbs_count) {
        ++tbs_idx; /* Skip */
    }

    /* Issuer */
    if (tbs_idx < (int)tbs_count) {
        parse_name(tbs_nodes[tbs_idx].data, tbs_nodes[tbs_idx].length,
                   cert->issuer, sizeof(cert->issuer));
        ++tbs_idx;
    }

    /* Validity */
    if (tbs_idx < (int)tbs_count) {
        parse_validity(tbs_nodes[tbs_idx].data, tbs_nodes[tbs_idx].length,
                       &cert->not_before, &cert->not_after);
        ++tbs_idx;
    }

    /* Subject */
    if (tbs_idx < (int)tbs_count) {
        parse_name(tbs_nodes[tbs_idx].data, tbs_nodes[tbs_idx].length,
                   cert->subject, sizeof(cert->subject));
        ++tbs_idx;
    }

    /* SubjectPublicKeyInfo */
    if (tbs_idx < (int)tbs_count) {
        asn1_node_t spki_nodes[8];
        size_t spki_count = 8;
        if (asn1_decode_sequence(tbs_nodes[tbs_idx].data, tbs_nodes[tbs_idx].length,
                                 spki_nodes, &spki_count) >= 0 && spki_count >= 2) {
            /* spki_nodes[0] = AlgorithmIdentifier */
            int key_type = parse_algorithm_identifier(spki_nodes[0].data, spki_nodes[0].length);
            cert->key_type = (key_type == 1) ? X509_KEY_EC : X509_KEY_RSA;

            /* spki_nodes[1] = subjectPublicKey (BIT STRING) */
            asn1_node_t pubkey_node;
            if (asn1_decode(spki_nodes[1].data, spki_nodes[1].length, &pubkey_node) >= 0) {
                if ((pubkey_node.tag & 0x1F) == ASN1_TAG_BIT_STRING) {
                    size_t bit_string_data_len = pubkey_node.length - 1;
                    const uint8_t *bit_string_data = pubkey_node.data + 1;

                    if (cert->key_type == X509_KEY_RSA) {
                        /* Parse RSAPublicKey SEQUENCE { modulus, exponent } */
                        asn1_node_t rsa_nodes[4];
                        size_t rsa_count = 4;
                        if (asn1_decode_sequence(bit_string_data, bit_string_data_len,
                                                 rsa_nodes, &rsa_count) >= 0 && rsa_count >= 2) {
                            asn1_node_t n_node, e_node;
                            if (asn1_decode(rsa_nodes[0].data, rsa_nodes[0].length, &n_node) >= 0 &&
                                asn1_decode(rsa_nodes[1].data, rsa_nodes[1].length, &e_node) >= 0) {
                                cert->modulus_len = n_node.length;
                                if (cert->modulus_len > sizeof(cert->modulus))
                                    cert->modulus_len = sizeof(cert->modulus);
                                memcpy(cert->modulus, n_node.data, cert->modulus_len);
                                cert->key_bits = cert->modulus_len * 8;

                                cert->exponent_len = e_node.length;
                                if (cert->exponent_len > sizeof(cert->exponent))
                                    cert->exponent_len = sizeof(cert->exponent);
                                memcpy(cert->exponent, e_node.data, cert->exponent_len);
                            }
                        }
                    } else if (cert->key_type == X509_KEY_EC) {
                        cert->ec_point_len = bit_string_data_len;
                        if (cert->ec_point_len > sizeof(cert->ec_point))
                            cert->ec_point_len = sizeof(cert->ec_point);
                        memcpy(cert->ec_point, bit_string_data, cert->ec_point_len);
                        cert->key_bits = (cert->ec_point_len - 1) * 4;
                    }
                }
            }
        }
    }

    return 0;
}

int x509_parse_certificate_pem(const char *pem, size_t len, x509_cert_t *cert)
{
    if (pem == NULL || cert == NULL) return -1;

    /* Find BEGIN and END markers */
    const char *start = strstr(pem, "-----BEGIN CERTIFICATE-----");
    if (start == NULL) return -1;
    start += 27;

    const char *end = strstr(start, "-----END CERTIFICATE-----");
    if (end == NULL) return -1;

    /* Skip whitespace */
    while (start < end && (*start == '\r' || *start == '\n' || *start == ' ')) {
        ++start;
    }

    size_t b64_len = (size_t)(end - start);
    char *b64 = (char *)start;

    /* Decode base64 */
    uint8_t der[4096];
    size_t der_len = 0;

    /* Remove newlines from base64 */
    char clean[4096];
    size_t clean_len = 0;
    for (size_t i = 0; i < b64_len && clean_len < sizeof(clean) - 1; ++i) {
        if (b64[i] != '\r' && b64[i] != '\n' && b64[i] != ' ' && b64[i] != '\t') {
            clean[clean_len++] = b64[i];
        }
    }
    clean[clean_len] = '\0';

    if (crypto_base64_decode(clean, der, sizeof(der), &der_len) < 0) {
        return -1;
    }

    return x509_parse_certificate(der, der_len, cert);
}
