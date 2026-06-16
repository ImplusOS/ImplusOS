#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASN1_CLASS_UNIVERSAL   0x00
#define ASN1_CLASS_APPLICATION 0x40
#define ASN1_CLASS_CONTEXT     0x80
#define ASN1_CLASS_PRIVATE     0xC0

#define ASN1_TAG_BOOLEAN       0x01
#define ASN1_TAG_INTEGER       0x02
#define ASN1_TAG_BIT_STRING    0x03
#define ASN1_TAG_OCTET_STRING  0x04
#define ASN1_TAG_NULL          0x05
#define ASN1_TAG_OID           0x06
#define ASN1_TAG_UTF8_STRING   0x0C
#define ASN1_TAG_SEQUENCE      0x10
#define ASN1_TAG_SET           0x11
#define ASN1_TAG_PRINTABLE_STRING 0x13
#define ASN1_TAG_IA5_STRING    0x16
#define ASN1_TAG_UTC_TIME      0x17
#define ASN1_TAG_GENERALIZED_TIME 0x18

#define ASN1_CONSTRUCTED       0x20

typedef struct {
    uint8_t tag;
    uint8_t constructed;
    size_t length;
    const uint8_t *data;
} asn1_node_t;

int asn1_decode(const uint8_t *buf, size_t len, asn1_node_t *node);
int asn1_decode_sequence(const uint8_t *buf, size_t len, asn1_node_t *nodes, size_t *count);
int asn1_decode_sequence_of(const uint8_t *buf, size_t len, uint8_t expected_tag, asn1_node_t *nodes, size_t *count);

int asn1_encode_tag(uint8_t *buf, size_t buf_size, uint8_t tag, const uint8_t *data, size_t data_len, size_t *out_len);
int asn1_encode_integer(uint8_t *buf, size_t buf_size, const uint8_t *value, size_t value_len, size_t *out_len);
int asn1_encode_octet_string(uint8_t *buf, size_t buf_size, const uint8_t *data, size_t data_len, size_t *out_len);
int asn1_encode_bit_string(uint8_t *buf, size_t buf_size, const uint8_t *data, size_t data_len, size_t *out_len);
int asn1_encode_sequence(uint8_t *buf, size_t buf_size, const uint8_t *data, size_t data_len, size_t *out_len);
int asn1_encode_oid(uint8_t *buf, size_t buf_size, const uint32_t *arcs, size_t arc_count, size_t *out_len);
int asn1_encode_null(uint8_t *buf, size_t buf_size, size_t *out_len);

#ifdef __cplusplus
}
#endif
