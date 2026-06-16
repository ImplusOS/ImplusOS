#include "OCSP_CRL.h"
#include "../ASN1/ASN1.h"
#include <string.h>

int ocsp_parse_response(const uint8_t *der, size_t len, ocsp_response_t *resp)
{
    if (der == NULL || resp == NULL) return -1;
    memset(resp, 0, sizeof(ocsp_response_t));

    asn1_node_t outer[4];
    size_t outer_count = 4;
    if (asn1_decode_sequence(der, len, outer, &outer_count) < 0) return -1;
    if (outer_count < 1) return -1;

    asn1_node_t response_nodes[8];
    size_t resp_count = 8;
    if (asn1_decode_sequence(outer[0].data, outer[0].length, response_nodes, &resp_count) < 0) {
        return -1;
    }

    /* Look for responseBytes context-specific tag */
    for (size_t j = 0; j < resp_count; ++j) {
        asn1_node_t ctx;
        if (asn1_decode(response_nodes[j].data, response_nodes[j].length, &ctx) < 0) continue;

        /* Context-specific tag 0: BasicOCSPResponse is SEQUENCE */
        if (ctx.tag == 0x00) {
            asn1_node_t basic_nodes[8];
            size_t basic_count = 8;
            if (asn1_decode_sequence(ctx.data, ctx.length, basic_nodes, &basic_count) < 0) continue;
            if (basic_count < 1) continue;

            /* basic_nodes[0] = tbsResponseData */
            asn1_node_t tbs_nodes[8];
            size_t tbs_count = 8;
            if (asn1_decode_sequence(basic_nodes[0].data, basic_nodes[0].length,
                                     tbs_nodes, &tbs_count) < 0) continue;

            /* Look for responses in tbs */
            for (size_t k = 0; k < tbs_count; ++k) {
                asn1_node_t resp_node;
                if (asn1_decode(tbs_nodes[k].data, tbs_nodes[k].length, &resp_node) < 0) continue;

                /* singleResponse is SEQUENCE */
                if ((resp_node.tag & 0x1F) == ASN1_TAG_SEQUENCE && resp_node.constructed) {
                    asn1_node_t single_nodes[8];
                    size_t single_count = 8;
                    if (asn1_decode_sequence(resp_node.data, resp_node.length,
                                             single_nodes, &single_count) < 0) continue;
                    if (single_count < 2) continue;

                    /* single_nodes[0] = CertID { hashAlgorithm, issuerNameHash, issuerKeyHash, serialNumber } */
                    asn1_node_t cert_id_nodes[8];
                    size_t cert_id_count = 8;
                    if (asn1_decode_sequence(single_nodes[0].data, single_nodes[0].length,
                                             cert_id_nodes, &cert_id_count) < 0) continue;
                    if (cert_id_count < 4) continue;

                    /* cert_id_nodes[1] = issuerNameHash (OCTET STRING) */
                    asn1_node_t name_hash;
                    if (asn1_decode(cert_id_nodes[1].data, cert_id_nodes[1].length, &name_hash) < 0) continue;
                    if (name_hash.length <= 32) {
                        memcpy(resp->issuer_name_hash, name_hash.data, name_hash.length);
                    }

                    /* cert_id_nodes[2] = issuerKeyHash (OCTET STRING) */
                    asn1_node_t key_hash;
                    if (asn1_decode(cert_id_nodes[2].data, cert_id_nodes[2].length, &key_hash) < 0) continue;
                    if (key_hash.length <= 32) {
                        memcpy(resp->issuer_key_hash, key_hash.data, key_hash.length);
                    }

                    /* cert_id_nodes[3] = serialNumber (INTEGER) */
                    asn1_node_t serial_node;
                    if (asn1_decode(cert_id_nodes[3].data, cert_id_nodes[3].length, &serial_node) < 0) continue;
                    if (resp->num_responses < 8) {
                        size_t idx = resp->num_responses++;
                        resp->responses[idx].serial_len = serial_node.length;
                        if (resp->responses[idx].serial_len > 32)
                            resp->responses[idx].serial_len = 32;
                        memcpy(resp->responses[idx].serial, serial_node.data,
                               resp->responses[idx].serial_len);
                        resp->responses[idx].status = OCSP_CERT_GOOD;

                        /* single_nodes[1] = certStatus (context-specific) */
                        asn1_node_t status_node;
                        if (asn1_decode(single_nodes[1].data, single_nodes[1].length, &status_node) >= 0) {
                            if (status_node.tag == 0x00) {
                                resp->responses[idx].status = OCSP_CERT_GOOD;
                            } else if (status_node.tag == 0x01) {
                                resp->responses[idx].status = OCSP_CERT_REVOKED;
                            } else {
                                resp->responses[idx].status = OCSP_CERT_UNKNOWN;
                            }
                        }
                    }
                }
            }
        }
    }

    if (resp->num_responses == 0) return -1;
    return 0;
}

int ocsp_check_cert_status(const ocsp_response_t *resp, const uint8_t *serial, size_t serial_len)
{
    if (resp == NULL || serial == NULL) return OCSP_CERT_UNKNOWN;

    for (size_t i = 0; i < resp->num_responses; ++i) {
        if (resp->responses[i].serial_len == serial_len &&
            memcmp(resp->responses[i].serial, serial, serial_len) == 0) {
            return resp->responses[i].status;
        }
    }

    return OCSP_CERT_UNKNOWN;
}

int crl_parse(const uint8_t *der, size_t len, crl_t *crl)
{
    if (der == NULL || crl == NULL) return -1;
    memset(crl, 0, sizeof(crl_t));

    asn1_node_t outer[4];
    size_t outer_count = 4;
    if (asn1_decode_sequence(der, len, outer, &outer_count) < 0) return -1;
    if (outer_count < 1) return -1;

    /* Parse TBSCertList */
    asn1_node_t tbs_nodes[16];
    size_t tbs_count = 16;
    if (asn1_decode_sequence(outer[0].data, outer[0].length, tbs_nodes, &tbs_count) < 0) return -1;

    /* Find revokedCertificates SEQUENCE */
    for (size_t i = 0; i < tbs_count; ++i) {
        asn1_node_t node;
        if (asn1_decode(tbs_nodes[i].data, tbs_nodes[i].length, &node) < 0) continue;

        if ((node.tag & 0x1F) == ASN1_TAG_SEQUENCE && node.constructed) {
            /* This might be the revoked certificates list */
            asn1_node_t rev_nodes[64];
            size_t rev_count = 64;
            if (asn1_decode_sequence(node.data, node.length, rev_nodes, &rev_count) < 0) continue;

            for (size_t j = 0; j < rev_count && crl->num_entries < 64; ++j) {
                asn1_node_t entry_nodes[4];
                size_t entry_count = 4;
                if (asn1_decode_sequence(rev_nodes[j].data, rev_nodes[j].length,
                                         entry_nodes, &entry_count) < 0) continue;
                if (entry_count < 2) continue;

                /* entry_nodes[0] = serialNumber */
                asn1_node_t serial_node;
                if (asn1_decode(entry_nodes[0].data, entry_nodes[0].length, &serial_node) < 0) continue;

                size_t idx = crl->num_entries++;
                crl->entries[idx].serial_len = serial_node.length;
                if (crl->entries[idx].serial_len > 32)
                    crl->entries[idx].serial_len = 32;
                memcpy(crl->entries[idx].serial, serial_node.data, crl->entries[idx].serial_len);
            }
        }
    }

    return (crl->num_entries > 0) ? 0 : -1;
}

int crl_is_revoked(const crl_t *crl, const uint8_t *serial, size_t serial_len)
{
    if (crl == NULL || serial == NULL) return 0;

    for (size_t i = 0; i < crl->num_entries; ++i) {
        if (crl->entries[i].serial_len == serial_len &&
            memcmp(crl->entries[i].serial, serial, serial_len) == 0) {
            return 1;
        }
    }

    return 0;
}
