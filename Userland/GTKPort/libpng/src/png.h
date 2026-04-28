#pragma once

#include <stddef.h>
#include <stdint.h>

#define PNG_LIBPNG_VER_STRING "1.6.43"
#define PNG_LIBPNG_VER 10643

#define PNG_COLOR_MASK_COLOR   2
#define PNG_COLOR_MASK_ALPHA   4
#define PNG_COLOR_TYPE_GRAY       0
#define PNG_COLOR_TYPE_RGB        PNG_COLOR_MASK_COLOR
#define PNG_COLOR_TYPE_PALETTE    (PNG_COLOR_MASK_COLOR | 1)
#define PNG_COLOR_TYPE_GRAY_ALPHA PNG_COLOR_MASK_ALPHA
#define PNG_COLOR_TYPE_RGB_ALPHA  (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_ALPHA)
#define PNG_INTERLACE_NONE  0
#define PNG_COMPRESSION_TYPE_DEFAULT 0
#define PNG_FILTER_TYPE_DEFAULT      0
#define PNG_TRANSFORM_IDENTITY 0
#define PNG_INFO_tRNS 0x0010

typedef struct png_struct_def  png_struct;
typedef struct png_info_def    png_info;
typedef png_struct  *png_structp;
typedef png_info    *png_infop;
typedef png_struct **png_structpp;
typedef png_info   **png_infopp;
typedef unsigned char  png_byte;
typedef unsigned char *png_bytep;
typedef const char    *png_const_charp;
typedef uint32_t       png_uint_32;
typedef void          *png_voidp;
typedef const void    *png_const_voidp;
typedef size_t         png_size_t;
typedef png_byte     **png_bytepp;

typedef void (*png_error_ptr)(png_structp, png_const_charp);
typedef void (*png_rw_ptr)(png_structp, png_bytep, png_size_t);
typedef void (*png_flush_ptr)(png_structp);

png_structp png_create_read_struct(png_const_charp ver, png_voidp err_ptr,
                                   png_error_ptr err_fn, png_error_ptr warn_fn);
png_infop   png_create_info_struct(png_structp png_ptr);
void        png_destroy_read_struct(png_structpp pp, png_infopp ip, png_infopp ep);
void        png_set_read_fn(png_structp pp, png_voidp io, png_rw_ptr fn);
void        png_read_info(png_structp pp, png_infop ip);
void        png_read_image(png_structp pp, png_bytepp rows);
void        png_read_end(png_structp pp, png_infop ip);
png_uint_32 png_get_image_width(png_const_voidp pp, png_const_voidp ip);
png_uint_32 png_get_image_height(png_const_voidp pp, png_const_voidp ip);
png_byte    png_get_color_type(png_const_voidp pp, png_const_voidp ip);
png_byte    png_get_bit_depth(png_const_voidp pp, png_const_voidp ip);
void        png_set_expand(png_structp pp);
void        png_set_strip_16(png_structp pp);
void        png_set_gray_to_rgb(png_structp pp);
void        png_set_add_alpha(png_structp pp, unsigned int filler, int flags);
png_uint_32 png_get_valid(png_const_voidp pp, png_const_voidp ip, png_uint_32 flag);
void        png_set_tRNS_to_alpha(png_structp pp);

png_structp png_create_write_struct(png_const_charp ver, png_voidp err_ptr,
                                    png_error_ptr err_fn, png_error_ptr warn_fn);
void        png_destroy_write_struct(png_structpp pp, png_infopp ip);
void        png_set_write_fn(png_structp pp, png_voidp io, png_rw_ptr fn, png_flush_ptr fl);
void        png_set_IHDR(png_structp pp, png_infop ip, png_uint_32 w, png_uint_32 h,
                         int bd, int ct, int il, int cm, int fm);
void        png_write_info(png_structp pp, png_infop ip);
void        png_write_image(png_structp pp, png_bytepp rows);
void        png_write_end(png_structp pp, png_infop ip);

#define png_jmpbuf(p) ((void*)0)
