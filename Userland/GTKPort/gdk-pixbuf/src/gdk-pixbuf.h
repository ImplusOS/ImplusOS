#pragma once

#include <stddef.h>
#include <stdint.h>

typedef unsigned long GType;
typedef int gboolean;
typedef struct _GError GError;

typedef struct _GdkPixbuf GdkPixbuf;
typedef struct _GdkPixbufLoader GdkPixbufLoader;

typedef enum { GDK_COLORSPACE_RGB = 0 } GdkColorspace;
typedef enum { GDK_PIXBUF_ALPHA_BILEVEL=0, GDK_PIXBUF_ALPHA_FULL } GdkPixbufAlphaMode;
typedef enum { GDK_INTERP_NEAREST=0, GDK_INTERP_TILES, GDK_INTERP_BILINEAR, GDK_INTERP_HYPER } GdkInterpType;

GdkPixbuf *gdk_pixbuf_new(GdkColorspace colorspace, gboolean has_alpha, int bits_per_sample, int width, int height);
GdkPixbuf *gdk_pixbuf_new_from_file(const char *filename, GError **error);
GdkPixbuf *gdk_pixbuf_new_from_data(const unsigned char *data, GdkColorspace colorspace,
                                     gboolean has_alpha, int bits_per_sample,
                                     int width, int height, int rowstride,
                                     void (*destroy_fn)(unsigned char*, void*), void *destroy_fn_data);
GdkPixbuf *gdk_pixbuf_new_from_resource(const char *resource_path, GError **error);
GdkPixbuf *gdk_pixbuf_new_subpixbuf(GdkPixbuf *src_pixbuf, int src_x, int src_y, int width, int height);
GdkPixbuf *gdk_pixbuf_copy(const GdkPixbuf *pixbuf);
GdkPixbuf *gdk_pixbuf_scale_simple(const GdkPixbuf *src, int dest_width, int dest_height, GdkInterpType interp_type);

void gdk_pixbuf_scale(const GdkPixbuf *src, GdkPixbuf *dest, int dest_x, int dest_y,
                      int dest_width, int dest_height, double offset_x, double offset_y,
                      double scale_x, double scale_y, GdkInterpType interp_type);

int            gdk_pixbuf_get_width(const GdkPixbuf *pixbuf);
int            gdk_pixbuf_get_height(const GdkPixbuf *pixbuf);
int            gdk_pixbuf_get_rowstride(const GdkPixbuf *pixbuf);
int            gdk_pixbuf_get_n_channels(const GdkPixbuf *pixbuf);
int            gdk_pixbuf_get_bits_per_sample(const GdkPixbuf *pixbuf);
gboolean       gdk_pixbuf_get_has_alpha(const GdkPixbuf *pixbuf);
unsigned char *gdk_pixbuf_get_pixels(const GdkPixbuf *pixbuf);
GdkColorspace  gdk_pixbuf_get_colorspace(const GdkPixbuf *pixbuf);

GdkPixbuf *gdk_pixbuf_ref(GdkPixbuf *pixbuf);
void       gdk_pixbuf_unref(GdkPixbuf *pixbuf);

void gdk_pixbuf_fill(GdkPixbuf *pixbuf, uint32_t pixel);
gboolean gdk_pixbuf_save(GdkPixbuf *pixbuf, const char *filename, const char *type, GError **error, ...);

GType gdk_pixbuf_get_type(void);

GdkPixbufLoader *gdk_pixbuf_loader_new(void);
GdkPixbufLoader *gdk_pixbuf_loader_new_with_type(const char *image_type, GError **error);
gboolean         gdk_pixbuf_loader_write(GdkPixbufLoader *loader, const unsigned char *buf, size_t count, GError **error);
GdkPixbuf       *gdk_pixbuf_loader_get_pixbuf(GdkPixbufLoader *loader);
gboolean         gdk_pixbuf_loader_close(GdkPixbufLoader *loader, GError **error);
