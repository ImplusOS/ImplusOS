#include "gdk-pixbuf.h"
#include <string.h>

extern void *calloc(unsigned long, unsigned long);
extern void *malloc(unsigned long);
extern void  free(void *);

struct _GdkPixbuf {
    unsigned char *pixels;
    int width, height, rowstride, n_channels, bits_per_sample;
    int has_alpha, owns_data, ref_count;
};

struct _GdkPixbufLoader { GdkPixbuf *pixbuf; int closed; };

GdkPixbuf *gdk_pixbuf_new(GdkColorspace cs, gboolean ha, int bps, int w, int h) {
    (void)cs;
    GdkPixbuf *p = (GdkPixbuf*)calloc(1, sizeof(*p));
    p->width = w; p->height = h; p->bits_per_sample = bps;
    p->has_alpha = ha; p->n_channels = ha ? 4 : 3;
    p->rowstride = w * p->n_channels;
    p->pixels = (unsigned char*)calloc(1, (unsigned long)p->rowstride * (unsigned long)h);
    p->owns_data = 1; p->ref_count = 1;
    return p;
}

GdkPixbuf *gdk_pixbuf_new_from_file(const char *fn, GError **e) {
    (void)fn;(void)e;
    return gdk_pixbuf_new(GDK_COLORSPACE_RGB, 1, 8, 1, 1);
}

GdkPixbuf *gdk_pixbuf_new_from_data(const unsigned char *data, GdkColorspace cs,
                                     gboolean ha, int bps, int w, int h, int rs,
                                     void (*df)(unsigned char*, void*), void *dfd) {
    (void)cs;(void)df;(void)dfd;
    GdkPixbuf *p = (GdkPixbuf*)calloc(1, sizeof(*p));
    p->pixels = (unsigned char*)data; p->width = w; p->height = h;
    p->rowstride = rs; p->bits_per_sample = bps;
    p->has_alpha = ha; p->n_channels = ha ? 4 : 3;
    p->owns_data = 0; p->ref_count = 1;
    return p;
}

GdkPixbuf *gdk_pixbuf_new_from_resource(const char *rp, GError **e) { return gdk_pixbuf_new_from_file(rp, e); }

GdkPixbuf *gdk_pixbuf_new_subpixbuf(GdkPixbuf *src, int sx, int sy, int w, int h) {
    (void)sx;(void)sy;
    GdkPixbuf *p = gdk_pixbuf_new(GDK_COLORSPACE_RGB, src->has_alpha, src->bits_per_sample, w, h);
    return p;
}

GdkPixbuf *gdk_pixbuf_copy(const GdkPixbuf *src) {
    GdkPixbuf *p = gdk_pixbuf_new(GDK_COLORSPACE_RGB, src->has_alpha, src->bits_per_sample, src->width, src->height);
    memcpy(p->pixels, src->pixels, (unsigned long)src->rowstride * (unsigned long)src->height);
    return p;
}

GdkPixbuf *gdk_pixbuf_scale_simple(const GdkPixbuf *src, int dw, int dh, GdkInterpType it) {
    (void)src;(void)it;
    return gdk_pixbuf_new(GDK_COLORSPACE_RGB, src->has_alpha, src->bits_per_sample, dw, dh);
}

void gdk_pixbuf_scale(const GdkPixbuf *src, GdkPixbuf *dest, int dx, int dy,
                      int dw, int dh, double ox, double oy, double sx, double sy, GdkInterpType it) {
    (void)src;(void)dest;(void)dx;(void)dy;(void)dw;(void)dh;(void)ox;(void)oy;(void)sx;(void)sy;(void)it;
}

int gdk_pixbuf_get_width(const GdkPixbuf *p) { return p->width; }
int gdk_pixbuf_get_height(const GdkPixbuf *p) { return p->height; }
int gdk_pixbuf_get_rowstride(const GdkPixbuf *p) { return p->rowstride; }
int gdk_pixbuf_get_n_channels(const GdkPixbuf *p) { return p->n_channels; }
int gdk_pixbuf_get_bits_per_sample(const GdkPixbuf *p) { return p->bits_per_sample; }
gboolean gdk_pixbuf_get_has_alpha(const GdkPixbuf *p) { return p->has_alpha; }
unsigned char *gdk_pixbuf_get_pixels(const GdkPixbuf *p) { return p->pixels; }
GdkColorspace gdk_pixbuf_get_colorspace(const GdkPixbuf *p) { (void)p; return GDK_COLORSPACE_RGB; }

GdkPixbuf *gdk_pixbuf_ref(GdkPixbuf *p) { if (p) p->ref_count++; return p; }
void gdk_pixbuf_unref(GdkPixbuf *p) {
    if (!p || --p->ref_count > 0) return;
    if (p->owns_data) free(p->pixels);
    free(p);
}

void gdk_pixbuf_fill(GdkPixbuf *p, uint32_t pixel) {
    if (!p || !p->pixels) return;
    unsigned char r = (pixel >> 24) & 0xFF, g = (pixel >> 16) & 0xFF, b = (pixel >> 8) & 0xFF, a = pixel & 0xFF;
    for (int y = 0; y < p->height; y++) {
        unsigned char *row = p->pixels + y * p->rowstride;
        for (int x = 0; x < p->width; x++) {
            row[x * p->n_channels] = r;
            row[x * p->n_channels + 1] = g;
            row[x * p->n_channels + 2] = b;
            if (p->has_alpha) row[x * p->n_channels + 3] = a;
        }
    }
}

gboolean gdk_pixbuf_save(GdkPixbuf *p, const char *fn, const char *t, GError **e, ...) { (void)p;(void)fn;(void)t;(void)e; return 0; }
GType gdk_pixbuf_get_type(void) { return 200; }

GdkPixbufLoader *gdk_pixbuf_loader_new(void) { return (GdkPixbufLoader*)calloc(1, sizeof(GdkPixbufLoader)); }
GdkPixbufLoader *gdk_pixbuf_loader_new_with_type(const char *t, GError **e) { (void)t;(void)e; return gdk_pixbuf_loader_new(); }
gboolean gdk_pixbuf_loader_write(GdkPixbufLoader *l, const unsigned char *b, size_t c, GError **e) { (void)l;(void)b;(void)c;(void)e; return 1; }
GdkPixbuf *gdk_pixbuf_loader_get_pixbuf(GdkPixbufLoader *l) {
    if (!l->pixbuf) l->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, 1, 8, 1, 1);
    return l->pixbuf;
}
gboolean gdk_pixbuf_loader_close(GdkPixbufLoader *l, GError **e) { (void)e; l->closed = 1; return 1; }
