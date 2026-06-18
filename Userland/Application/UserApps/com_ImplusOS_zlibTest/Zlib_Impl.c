#include <stddef.h>
#include <Zlib.h>
#include "../../../../Vendor/Library/zlib/zconf.h"
#include "../../../../Vendor/Library/zlib/zlib.h"
#include <stdlib.h>
#include <string.h>

int zlib_compress(const uint8_t *in, size_t in_len, zlib_buffer_t *out) {
    if (!in || !out) return -1;

    uLongf destLen = compressBound((uLong)in_len);
    out->data = (uint8_t *)malloc(destLen);
    if (!out->data) return -2;

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    int res = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                           15, 5, Z_DEFAULT_STRATEGY);
    if (res != Z_OK) {
        free(out->data);
        out->data = NULL;
        return res;
    }

    stream.next_in = (Bytef *)in;
    stream.avail_in = (uInt)in_len;
    stream.next_out = out->data;
    stream.avail_out = (uInt)destLen;

    res = deflate(&stream, Z_FINISH);
    if (res != Z_STREAM_END) {
        int end_res = deflateEnd(&stream);
        (void)end_res;
        free(out->data);
        out->data = NULL;
        return res;
    }

    out->size = (size_t)stream.total_out;
    (void)deflateEnd(&stream);
    return 0;
}

int zlib_decompress(const uint8_t *in, size_t in_len, zlib_buffer_t *out) {
    if (!in || !out) return -1;
    
    uLongf destLen = (uLong)(in_len * 4); 
    out->data = (uint8_t *)malloc((size_t)destLen + 1u);
    if (!out->data) return -2;

    int res = uncompress(out->data, &destLen, in, (uLong)in_len);
    if (res != Z_OK) {
        free(out->data);
        out->data = NULL;
        return res;
    }

    out->size = (size_t)destLen;
    out->data[out->size] = '\0';
    return 0;
}

void zlib_free_buffer(zlib_buffer_t *buf) {
    if (buf && buf->data) {
        free(buf->data);
        buf->data = NULL;
        buf->size = 0;
    }
}
