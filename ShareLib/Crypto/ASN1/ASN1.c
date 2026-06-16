#include "ASN1.h"
#include <string.h>

int asn1_decode(const uint8_t *buf, size_t len, asn1_node_t *node)
{
    if (buf == NULL || node == NULL || len == 0) {
        return -1;
    }

    size_t offset = 0;
    uint8_t tag = buf[offset++];
    node->tag = tag & 0x1F;
    node->constructed = (tag & ASN1_CONSTRUCTED) ? 1 : 0;

    if (offset >= len) return -1;
    uint8_t first = buf[offset++];
    size_t length;

    if (first < 0x80) {
        length = first;
    } else if (first == 0x80) {
        return -1;
    } else {
        size_t num_bytes = first & 0x7F;
        if (num_bytes == 0 || num_bytes > sizeof(size_t) || offset + num_bytes > len) {
            return -1;
        }
        length = 0;
        for (size_t i = 0; i < num_bytes; ++i) {
            length = (length << 8) | buf[offset++];
        }
    }

    if (offset + length > len) {
        return -1;
    }

    node->length = length;
    node->data = buf + offset;
    return (int)(offset + length);
}

int asn1_decode_sequence(const uint8_t *buf, size_t len, asn1_node_t *nodes, size_t *count)
{
    if (buf == NULL || nodes == NULL || count == NULL) {
        return -1;
    }

    asn1_node_t seq;
    int ret = asn1_decode(buf, len, &seq);
    if (ret < 0) return -1;
    if ((seq.tag & 0x1F) != ASN1_TAG_SEQUENCE || !seq.constructed) {
        return -1;
    }

    const uint8_t *p = seq.data;
    size_t remaining = seq.length;
    size_t idx = 0;
    size_t max_count = *count;

    while (remaining > 0 && idx < max_count) {
        asn1_node_t *child = &nodes[idx];
        int consumed = asn1_decode(p, remaining, child);
        if (consumed <= 0) break;
        p += consumed;
        remaining -= (size_t)consumed;
        ++idx;
    }

    *count = idx;
    if (remaining > 0) {
        return -1;
    }
    return ret;
}

int asn1_decode_sequence_of(const uint8_t *buf, size_t len, uint8_t expected_tag, asn1_node_t *nodes, size_t *count)
{
    if (buf == NULL || nodes == NULL || count == NULL) {
        return -1;
    }

    asn1_node_t seq;
    int ret = asn1_decode(buf, len, &seq);
    if (ret < 0) return -1;
    if ((seq.tag & 0x1F) != ASN1_TAG_SEQUENCE || !seq.constructed) {
        return -1;
    }

    const uint8_t *p = seq.data;
    size_t remaining = seq.length;
    size_t idx = 0;
    size_t max_count = *count;

    while (remaining > 0 && idx < max_count) {
        asn1_node_t child;
        int consumed = asn1_decode(p, remaining, &child);
        if (consumed <= 0) break;
        if ((child.tag & 0x1F) == expected_tag) {
            nodes[idx] = child;
            ++idx;
        }
        p += consumed;
        remaining -= (size_t)consumed;
    }

    *count = idx;
    return ret;
}

static int asn1_encode_length(uint8_t *buf, size_t buf_size, size_t length, size_t *out_len)
{
    if (length < 0x80) {
        if (buf != NULL && buf_size >= 1) {
            buf[0] = (uint8_t)length;
        }
        *out_len = 1;
        return 0;
    }

    size_t num_bytes = 1;
    size_t tmp = length;
    while (tmp > 0xFF) {
        tmp >>= 8;
        ++num_bytes;
    }

    if (buf != NULL) {
        if (buf_size < 1 + num_bytes) return -1;
        buf[0] = (uint8_t)(0x80 | num_bytes);
        for (size_t i = 0; i < num_bytes; ++i) {
            buf[1 + i] = (uint8_t)(length >> ((num_bytes - 1 - i) * 8));
        }
    }
    *out_len = 1 + num_bytes;
    return 0;
}

int asn1_encode_tag(uint8_t *buf, size_t buf_size, uint8_t tag, const uint8_t *data, size_t data_len, size_t *out_len)
{
    if (out_len == NULL) return -1;
    if (data == NULL && data_len != 0) return -1;

    size_t len_len;
    if (asn1_encode_length(NULL, 0, data_len, &len_len) < 0) return -1;

    size_t total = 1 + len_len + data_len;
    if (buf != NULL) {
        if (buf_size < total) return -1;
        buf[0] = tag;
        size_t written;
        asn1_encode_length(buf + 1, buf_size - 1, data_len, &written);
        if (data_len > 0) {
            memcpy(buf + 1 + written, data, data_len);
        }
    }
    *out_len = total;
    return 0;
}

int asn1_encode_integer(uint8_t *buf, size_t buf_size, const uint8_t *value, size_t value_len, size_t *out_len)
{
    if (value == NULL || value_len == 0 || out_len == NULL) return -1;

    size_t pad = 0;
    if (value[0] & 0x80) {
        pad = 1;
    }

    size_t content_len = value_len + pad;
    uint8_t *content = NULL;
    uint8_t stack_buf[256];

    if (content_len <= sizeof(stack_buf)) {
        content = stack_buf;
    } else {
        return -1;
    }

    if (pad) {
        content[0] = 0x00;
        memcpy(content + 1, value, value_len);
    } else {
        memcpy(content, value, value_len);
    }

    int ret = asn1_encode_tag(buf, buf_size, ASN1_TAG_INTEGER, content, content_len, out_len);
    return ret;
}

int asn1_encode_octet_string(uint8_t *buf, size_t buf_size, const uint8_t *data, size_t data_len, size_t *out_len)
{
    return asn1_encode_tag(buf, buf_size, ASN1_TAG_OCTET_STRING, data, data_len, out_len);
}

int asn1_encode_bit_string(uint8_t *buf, size_t buf_size, const uint8_t *data, size_t data_len, size_t *out_len)
{
    if (data_len == 0) {
        uint8_t zero = 0;
        return asn1_encode_tag(buf, buf_size, ASN1_TAG_BIT_STRING, &zero, 1, out_len);
    }

    uint8_t *content = NULL;
    uint8_t stack_buf[1024];
    size_t content_len = data_len + 1;
    if (content_len <= sizeof(stack_buf)) {
        content = stack_buf;
    } else {
        return -1;
    }
    content[0] = 0;
    memcpy(content + 1, data, data_len);
    return asn1_encode_tag(buf, buf_size, ASN1_TAG_BIT_STRING, content, content_len, out_len);
}

int asn1_encode_sequence(uint8_t *buf, size_t buf_size, const uint8_t *data, size_t data_len, size_t *out_len)
{
    return asn1_encode_tag(buf, buf_size, ASN1_TAG_SEQUENCE | ASN1_CONSTRUCTED, data, data_len, out_len);
}

int asn1_encode_oid(uint8_t *buf, size_t buf_size, const uint32_t *arcs, size_t arc_count, size_t *out_len)
{
    if (arcs == NULL || arc_count < 2 || out_len == NULL) return -1;

    uint8_t content[128];
    size_t content_len = 0;

    content[content_len++] = (uint8_t)(arcs[0] * 40 + arcs[1]);

    for (size_t i = 2; i < arc_count; ++i) {
        uint32_t v = arcs[i];
        uint8_t tmp[5];
        size_t tmp_len = 0;

        if (v == 0) {
            tmp[tmp_len++] = 0;
        } else {
            uint8_t rev[5];
            size_t rev_len = 0;
            while (v > 0) {
                rev[rev_len++] = (uint8_t)(v & 0x7F);
                v >>= 7;
            }
            for (size_t j = rev_len; j > 0; --j) {
                uint8_t byte = rev[j - 1];
                if (j < rev_len) byte |= 0x80;
                tmp[tmp_len++] = byte;
            }
        }

        if (content_len + tmp_len > sizeof(content)) return -1;
        memcpy(content + content_len, tmp, tmp_len);
        content_len += tmp_len;
    }

    return asn1_encode_tag(buf, buf_size, ASN1_TAG_OID, content, content_len, out_len);
}

int asn1_encode_null(uint8_t *buf, size_t buf_size, size_t *out_len)
{
    return asn1_encode_tag(buf, buf_size, ASN1_TAG_NULL, NULL, 0, out_len);
}
