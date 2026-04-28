#include "png.h"
#include <string.h>

extern void *malloc(unsigned long);
extern void *calloc(unsigned long, unsigned long);
extern void  free(void *);

struct png_struct_def {
    png_error_ptr error_fn;
    png_error_ptr warning_fn;
    png_voidp     error_ptr;
    png_rw_ptr    read_fn;
    png_rw_ptr    write_fn;
    png_flush_ptr flush_fn;
    png_voidp     io_ptr;
    png_uint_32   width, height;
    png_byte      bit_depth, color_type;
    int           interlace_type;
};

struct png_info_def {
    png_uint_32 width, height;
    png_byte    bit_depth, color_type;
    png_byte    interlace_type;
    png_uint_32 valid;
};

png_structp png_create_read_struct(png_const_charp ver, png_voidp err_ptr,
                                   png_error_ptr err_fn, png_error_ptr warn_fn) {
    (void)ver;
    png_structp p = (png_structp)calloc(1, sizeof(png_struct));
    if (p) { p->error_fn = err_fn; p->warning_fn = warn_fn; p->error_ptr = err_ptr; }
    return p;
}

png_infop png_create_info_struct(png_structp pp) {
    (void)pp;
    return (png_infop)calloc(1, sizeof(png_info));
}

void png_destroy_read_struct(png_structpp pp, png_infopp ip, png_infopp ep) {
    if (pp && *pp) { free(*pp); *pp = 0; }
    if (ip && *ip) { free(*ip); *ip = 0; }
    if (ep && *ep) { free(*ep); *ep = 0; }
}

void png_set_read_fn(png_structp pp, png_voidp io, png_rw_ptr fn) {
    if (pp) { pp->read_fn = fn; pp->io_ptr = io; }
}

void png_read_info(png_structp pp, png_infop ip) {
    if (pp && ip) { ip->width = pp->width; ip->height = pp->height;
                    ip->bit_depth = pp->bit_depth; ip->color_type = pp->color_type; }
}

void png_read_image(png_structp pp, png_bytepp rows) { (void)pp; (void)rows; }
void png_read_end(png_structp pp, png_infop ip) { (void)pp; (void)ip; }

png_uint_32 png_get_image_width(png_const_voidp pp, png_const_voidp ip) {
    (void)pp; return ip ? ((const png_info*)ip)->width : 0;
}

png_uint_32 png_get_image_height(png_const_voidp pp, png_const_voidp ip) {
    (void)pp; return ip ? ((const png_info*)ip)->height : 0;
}

png_byte png_get_color_type(png_const_voidp pp, png_const_voidp ip) {
    (void)pp; return ip ? ((const png_info*)ip)->color_type : 0;
}

png_byte png_get_bit_depth(png_const_voidp pp, png_const_voidp ip) {
    (void)pp; return ip ? ((const png_info*)ip)->bit_depth : 0;
}

void png_set_expand(png_structp pp)   { (void)pp; }
void png_set_strip_16(png_structp pp) { (void)pp; }
void png_set_gray_to_rgb(png_structp pp) { (void)pp; }
void png_set_add_alpha(png_structp pp, unsigned int f, int fl) { (void)pp;(void)f;(void)fl; }
png_uint_32 png_get_valid(png_const_voidp pp, png_const_voidp ip, png_uint_32 f) {
    (void)pp;(void)ip; return f;
}
void png_set_tRNS_to_alpha(png_structp pp) { (void)pp; }

png_structp png_create_write_struct(png_const_charp ver, png_voidp err_ptr,
                                    png_error_ptr err_fn, png_error_ptr warn_fn) {
    (void)ver;
    png_structp p = (png_structp)calloc(1, sizeof(png_struct));
    if (p) { p->error_fn = err_fn; p->warning_fn = warn_fn; p->error_ptr = err_ptr; }
    return p;
}

void png_destroy_write_struct(png_structpp pp, png_infopp ip) {
    if (pp && *pp) { free(*pp); *pp = 0; }
    if (ip && *ip) { free(*ip); *ip = 0; }
}

void png_set_write_fn(png_structp pp, png_voidp io, png_rw_ptr fn, png_flush_ptr fl) {
    if (pp) { pp->write_fn = fn; pp->flush_fn = fl; pp->io_ptr = io; }
}

void png_set_IHDR(png_structp pp, png_infop ip, png_uint_32 w, png_uint_32 h,
                  int bd, int ct, int il, int cm, int fm) {
    (void)cm;(void)fm;
    if (pp) { pp->width = w; pp->height = h; pp->bit_depth = (png_byte)bd;
              pp->color_type = (png_byte)ct; pp->interlace_type = il; }
    if (ip) { ip->width = w; ip->height = h; ip->bit_depth = (png_byte)bd;
              ip->color_type = (png_byte)ct; ip->interlace_type = (png_byte)il; }
}

void png_write_info(png_structp pp, png_infop ip)        { (void)pp; (void)ip; }
void png_write_image(png_structp pp, png_bytepp rows)    { (void)pp; (void)rows; }
void png_write_end(png_structp pp, png_infop ip)         { (void)pp; (void)ip; }
