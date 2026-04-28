#pragma once

#include <stddef.h>

#define ZLIB_VERSION "1.3.1"
#define ZLIB_VERNUM  0x1310

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)

#define Z_NO_FLUSH      0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH    2
#define Z_FULL_FLUSH    3
#define Z_FINISH        4
#define Z_BLOCK         5
#define Z_TREES         6

#define Z_NO_COMPRESSION      0
#define Z_BEST_SPEED           1
#define Z_BEST_COMPRESSION     9
#define Z_DEFAULT_COMPRESSION (-1)

#define Z_DEFLATED 8

#define Z_NULL 0

#define MAX_WBITS 15

typedef unsigned char  Byte;
typedef unsigned char  Bytef;
typedef unsigned int   uInt;
typedef unsigned long  uLong;
typedef unsigned long  uLongf;
typedef void          *voidpf;
typedef void          *voidp;
typedef long           z_off_t;

typedef voidpf (*alloc_func)(voidpf opaque, uInt items, uInt size);
typedef void   (*free_func)(voidpf opaque, voidpf address);

typedef struct z_stream_s {
    const Bytef *next_in;
    uInt         avail_in;
    uLong        total_in;

    Bytef       *next_out;
    uInt         avail_out;
    uLong        total_out;

    const char  *msg;
    void        *state;

    alloc_func   zalloc;
    free_func    zfree;
    voidpf       opaque;

    int          data_type;
    uLong        adler;
    uLong        reserved;
} z_stream;

typedef z_stream *z_streamp;

uLong crc32(uLong crc, const Bytef *buf, uInt len);
uLong adler32(uLong adler, const Bytef *buf, uInt len);

int deflateInit2_(z_streamp strm, int level, int method, int windowBits, int memLevel, int strategy, const char *version, int stream_size);
int deflate(z_streamp strm, int flush);
int deflateEnd(z_streamp strm);

int inflateInit2_(z_streamp strm, int windowBits, const char *version, int stream_size);
int inflate(z_streamp strm, int flush);
int inflateEnd(z_streamp strm);
int inflateReset(z_streamp strm);

int compress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen);
int compress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level);
int uncompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen);

uLong compressBound(uLong sourceLen);

#define deflateInit(s,l) deflateInit2_((s),(l),Z_DEFLATED,MAX_WBITS,8,0,ZLIB_VERSION,(int)sizeof(z_stream))
#define inflateInit(s)   inflateInit2_((s),MAX_WBITS,ZLIB_VERSION,(int)sizeof(z_stream))
#define inflateInit2(s,w) inflateInit2_((s),(w),ZLIB_VERSION,(int)sizeof(z_stream))
#define deflateInit2(s,l,m,w,ml,st) deflateInit2_((s),(l),(m),(w),(ml),(st),ZLIB_VERSION,(int)sizeof(z_stream))
