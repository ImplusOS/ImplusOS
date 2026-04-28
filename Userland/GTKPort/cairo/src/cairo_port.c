#include "cairo.h"
#include <string.h>

extern void *calloc(unsigned long, unsigned long);
extern void *malloc(unsigned long);
extern void  free(void *);

struct _cairo_surface {
    unsigned char *data;
    int width, height, stride;
    cairo_format_t format;
    int ref_count;
    int owns_data;
    double device_x, device_y;
};

struct _cairo_pattern {
    double r, g, b, a;
    cairo_surface_t *surface;
    int ref_count;
};

struct _cairo_font_options {
    cairo_antialias_t antialias;
    cairo_hint_style_t hint_style;
    cairo_hint_metrics_t hint_metrics;
    cairo_subpixel_order_t subpixel_order;
};

struct _cairo_font_face { int ref_count; };
struct _cairo_scaled_font { cairo_font_face_t *face; int ref_count; };

struct _cairo {
    cairo_surface_t *target;
    double r, g, b, a;
    double line_width;
    cairo_operator_t op;
    cairo_antialias_t antialias;
    cairo_pattern_t *source;
    int ref_count;
    cairo_font_options_t font_options;
    cairo_scaled_font_t *scaled_font;
    cairo_matrix_t matrix;
};

int cairo_format_stride_for_width(cairo_format_t format, int width) {
    int bpp = 4;
    if (format == CAIRO_FORMAT_A8) bpp = 1;
    else if (format == CAIRO_FORMAT_A1) bpp = 1;
    else if (format == CAIRO_FORMAT_RGB16_565) bpp = 2;
    return (width * bpp + 3) & ~3;
}

cairo_surface_t *cairo_image_surface_create(cairo_format_t format, int width, int height) {
    cairo_surface_t *s = (cairo_surface_t*)calloc(1, sizeof(*s));
    s->format = format; s->width = width; s->height = height;
    s->stride = cairo_format_stride_for_width(format, width);
    s->data = (unsigned char*)calloc(1, (unsigned long)s->stride * (unsigned long)height);
    s->ref_count = 1; s->owns_data = 1;
    return s;
}

cairo_surface_t *cairo_image_surface_create_for_data(unsigned char *data, cairo_format_t format, int w, int h, int stride) {
    cairo_surface_t *s = (cairo_surface_t*)calloc(1, sizeof(*s));
    s->data = data; s->format = format; s->width = w; s->height = h; s->stride = stride;
    s->ref_count = 1; s->owns_data = 0;
    return s;
}

unsigned char *cairo_image_surface_get_data(cairo_surface_t *s) { return s ? s->data : NULL; }
int cairo_image_surface_get_width(cairo_surface_t *s) { return s ? s->width : 0; }
int cairo_image_surface_get_height(cairo_surface_t *s) { return s ? s->height : 0; }
int cairo_image_surface_get_stride(cairo_surface_t *s) { return s ? s->stride : 0; }
cairo_format_t cairo_image_surface_get_format(cairo_surface_t *s) { return s ? s->format : CAIRO_FORMAT_INVALID; }

cairo_surface_t *cairo_surface_reference(cairo_surface_t *s) { if (s) s->ref_count++; return s; }
void cairo_surface_destroy(cairo_surface_t *s) {
    if (!s || --s->ref_count > 0) return;
    if (s->owns_data) free(s->data);
    free(s);
}
void cairo_surface_flush(cairo_surface_t *s) { (void)s; }
void cairo_surface_mark_dirty(cairo_surface_t *s) { (void)s; }
cairo_status_t cairo_surface_status(cairo_surface_t *s) { return s ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_NULL_POINTER; }

cairo_surface_t *cairo_surface_create_similar(cairo_surface_t *o, cairo_content_t c, int w, int h) {
    (void)o; (void)c;
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
}

cairo_surface_t *cairo_surface_create_similar_image(cairo_surface_t *o, cairo_format_t f, int w, int h) {
    (void)o;
    return cairo_image_surface_create(f, w, h);
}

void cairo_surface_set_device_offset(cairo_surface_t *s, double x, double y) { if (s) { s->device_x = x; s->device_y = y; } }

cairo_t *cairo_create(cairo_surface_t *target) {
    cairo_t *cr = (cairo_t*)calloc(1, sizeof(*cr));
    cr->target = cairo_surface_reference(target);
    cr->a = 1.0; cr->line_width = 2.0; cr->ref_count = 1;
    cr->op = CAIRO_OPERATOR_OVER;
    cairo_matrix_init_identity(&cr->matrix);
    return cr;
}

void cairo_destroy(cairo_t *cr) {
    if (!cr || --cr->ref_count > 0) return;
    cairo_surface_destroy(cr->target);
    if (cr->source) cairo_pattern_destroy(cr->source);
    free(cr);
}

cairo_t *cairo_reference(cairo_t *cr) { if (cr) cr->ref_count++; return cr; }
cairo_status_t cairo_status(cairo_t *cr) { return cr ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_NULL_POINTER; }
cairo_surface_t *cairo_get_target(cairo_t *cr) { return cr ? cr->target : NULL; }
void cairo_save(cairo_t *cr) { (void)cr; }
void cairo_restore(cairo_t *cr) { (void)cr; }
void cairo_set_operator(cairo_t *cr, cairo_operator_t op) { cr->op = op; }
void cairo_set_source_rgb(cairo_t *cr, double r, double g, double b) { cr->r=r; cr->g=g; cr->b=b; cr->a=1.0; }
void cairo_set_source_rgba(cairo_t *cr, double r, double g, double b, double a) { cr->r=r; cr->g=g; cr->b=b; cr->a=a; }
void cairo_set_source_surface(cairo_t *cr, cairo_surface_t *s, double x, double y) { (void)cr;(void)s;(void)x;(void)y; }
void cairo_set_source(cairo_t *cr, cairo_pattern_t *s) { (void)cr;(void)s; }
cairo_pattern_t *cairo_get_source(cairo_t *cr) { return cr ? cr->source : NULL; }
void cairo_set_line_width(cairo_t *cr, double w) { cr->line_width = w; }
double cairo_get_line_width(cairo_t *cr) { return cr->line_width; }
void cairo_set_line_cap(cairo_t *cr, cairo_line_cap_t c) { (void)cr;(void)c; }
void cairo_set_line_join(cairo_t *cr, cairo_line_join_t j) { (void)cr;(void)j; }
void cairo_set_antialias(cairo_t *cr, cairo_antialias_t aa) { cr->antialias = aa; }
void cairo_set_fill_rule(cairo_t *cr, cairo_fill_rule_t r) { (void)cr;(void)r; }

void cairo_new_path(cairo_t *cr) { (void)cr; }
void cairo_new_sub_path(cairo_t *cr) { (void)cr; }
void cairo_close_path(cairo_t *cr) { (void)cr; }
void cairo_move_to(cairo_t *cr, double x, double y) { (void)cr;(void)x;(void)y; }
void cairo_line_to(cairo_t *cr, double x, double y) { (void)cr;(void)x;(void)y; }
void cairo_curve_to(cairo_t *cr, double x1, double y1, double x2, double y2, double x3, double y3) { (void)cr;(void)x1;(void)y1;(void)x2;(void)y2;(void)x3;(void)y3; }
void cairo_arc(cairo_t *cr, double xc, double yc, double r, double a1, double a2) { (void)cr;(void)xc;(void)yc;(void)r;(void)a1;(void)a2; }

void cairo_rectangle(cairo_t *cr, double x, double y, double w, double h) {
    (void)cr;(void)x;(void)y;(void)w;(void)h;
}

static void fill_rect_argb32(cairo_surface_t *s, int x, int y, int w, int h, uint32_t color) {
    if (!s || !s->data) return;
    for (int j = y; j < y + h && j < s->height; j++) {
        if (j < 0) continue;
        uint32_t *row = (uint32_t*)(s->data + j * s->stride);
        for (int i = x; i < x + w && i < s->width; i++) {
            if (i < 0) continue;
            row[i] = color;
        }
    }
}

void cairo_paint(cairo_t *cr) {
    if (!cr || !cr->target) return;
    uint32_t color = ((uint32_t)(cr->a*255)<<24)|((uint32_t)(cr->r*255)<<16)|((uint32_t)(cr->g*255)<<8)|(uint32_t)(cr->b*255);
    fill_rect_argb32(cr->target, 0, 0, cr->target->width, cr->target->height, color);
}

void cairo_paint_with_alpha(cairo_t *cr, double alpha) { (void)alpha; cairo_paint(cr); }
void cairo_fill(cairo_t *cr) { (void)cr; }
void cairo_fill_preserve(cairo_t *cr) { (void)cr; }
void cairo_stroke(cairo_t *cr) { (void)cr; }
void cairo_stroke_preserve(cairo_t *cr) { (void)cr; }
void cairo_clip(cairo_t *cr) { (void)cr; }
void cairo_clip_preserve(cairo_t *cr) { (void)cr; }
void cairo_reset_clip(cairo_t *cr) { (void)cr; }
void cairo_mask(cairo_t *cr, cairo_pattern_t *p) { (void)cr;(void)p; }
void cairo_mask_surface(cairo_t *cr, cairo_surface_t *s, double x, double y) { (void)cr;(void)s;(void)x;(void)y; }

void cairo_translate(cairo_t *cr, double tx, double ty) { (void)cr;(void)tx;(void)ty; }
void cairo_scale(cairo_t *cr, double sx, double sy) { (void)cr;(void)sx;(void)sy; }
void cairo_rotate(cairo_t *cr, double a) { (void)cr;(void)a; }
void cairo_transform(cairo_t *cr, const cairo_matrix_t *m) { (void)cr;(void)m; }
void cairo_set_matrix(cairo_t *cr, const cairo_matrix_t *m) { cr->matrix = *m; }
void cairo_get_matrix(cairo_t *cr, cairo_matrix_t *m) { *m = cr->matrix; }
void cairo_identity_matrix(cairo_t *cr) { cairo_matrix_init_identity(&cr->matrix); }

void cairo_matrix_init(cairo_matrix_t *m, double xx, double yx, double xy, double yy, double x0, double y0) { m->xx=xx;m->yx=yx;m->xy=xy;m->yy=yy;m->x0=x0;m->y0=y0; }
void cairo_matrix_init_identity(cairo_matrix_t *m) { cairo_matrix_init(m,1,0,0,1,0,0); }
void cairo_matrix_init_translate(cairo_matrix_t *m, double tx, double ty) { cairo_matrix_init(m,1,0,0,1,tx,ty); }
void cairo_matrix_init_scale(cairo_matrix_t *m, double sx, double sy) { cairo_matrix_init(m,sx,0,0,sy,0,0); }
void cairo_matrix_multiply(cairo_matrix_t *r, const cairo_matrix_t *a, const cairo_matrix_t *b) {
    cairo_matrix_t t;
    t.xx=a->xx*b->xx+a->yx*b->xy; t.yx=a->xx*b->yx+a->yx*b->yy;
    t.xy=a->xy*b->xx+a->yy*b->xy; t.yy=a->xy*b->yx+a->yy*b->yy;
    t.x0=a->x0*b->xx+a->y0*b->xy+b->x0; t.y0=a->x0*b->yx+a->y0*b->yy+b->y0;
    *r = t;
}
cairo_status_t cairo_matrix_invert(cairo_matrix_t *m) { (void)m; return CAIRO_STATUS_SUCCESS; }
void cairo_matrix_translate(cairo_matrix_t *m, double tx, double ty) { m->x0 += tx; m->y0 += ty; }
void cairo_matrix_scale(cairo_matrix_t *m, double sx, double sy) { m->xx *= sx; m->yy *= sy; }

void cairo_select_font_face(cairo_t *cr, const char *f, cairo_font_slant_t sl, cairo_font_weight_t w) { (void)cr;(void)f;(void)sl;(void)w; }
void cairo_set_font_size(cairo_t *cr, double s) { (void)cr;(void)s; }
void cairo_show_text(cairo_t *cr, const char *u) { (void)cr;(void)u; }
void cairo_text_extents(cairo_t *cr, const char *u, cairo_text_extents_t *e) { (void)cr;(void)u; if(e) memset(e,0,sizeof(*e)); }
void cairo_font_extents(cairo_t *cr, cairo_font_extents_t *e) { (void)cr; if(e) { e->ascent=12; e->descent=3; e->height=15; } }
void cairo_show_glyphs(cairo_t *cr, const cairo_glyph_t *g, int n) { (void)cr;(void)g;(void)n; }

cairo_pattern_t *cairo_pattern_create_rgb(double r, double g, double b) {
    cairo_pattern_t *p = (cairo_pattern_t*)calloc(1, sizeof(*p)); p->r=r;p->g=g;p->b=b;p->a=1;p->ref_count=1; return p;
}
cairo_pattern_t *cairo_pattern_create_rgba(double r, double g, double b, double a) {
    cairo_pattern_t *p = (cairo_pattern_t*)calloc(1, sizeof(*p)); p->r=r;p->g=g;p->b=b;p->a=a;p->ref_count=1; return p;
}
cairo_pattern_t *cairo_pattern_create_for_surface(cairo_surface_t *s) {
    cairo_pattern_t *p = (cairo_pattern_t*)calloc(1, sizeof(*p)); p->surface=s;p->a=1;p->ref_count=1; return p;
}
cairo_pattern_t *cairo_pattern_create_linear(double x0, double y0, double x1, double y1) {
    (void)x0;(void)y0;(void)x1;(void)y1;
    cairo_pattern_t *p = (cairo_pattern_t*)calloc(1, sizeof(*p)); p->a=1;p->ref_count=1; return p;
}
cairo_pattern_t *cairo_pattern_create_radial(double cx0, double cy0, double r0, double cx1, double cy1, double r1) {
    (void)cx0;(void)cy0;(void)r0;(void)cx1;(void)cy1;(void)r1;
    cairo_pattern_t *p = (cairo_pattern_t*)calloc(1, sizeof(*p)); p->a=1;p->ref_count=1; return p;
}
void cairo_pattern_destroy(cairo_pattern_t *p) { if (p && --p->ref_count <= 0) free(p); }
void cairo_pattern_add_color_stop_rgb(cairo_pattern_t *p, double o, double r, double g, double b) { (void)p;(void)o;(void)r;(void)g;(void)b; }
void cairo_pattern_add_color_stop_rgba(cairo_pattern_t *p, double o, double r, double g, double b, double a) { (void)p;(void)o;(void)r;(void)g;(void)b;(void)a; }
void cairo_pattern_set_extend(cairo_pattern_t *p, cairo_extend_t e) { (void)p;(void)e; }
void cairo_pattern_set_filter(cairo_pattern_t *p, cairo_filter_t f) { (void)p;(void)f; }
void cairo_pattern_set_matrix(cairo_pattern_t *p, const cairo_matrix_t *m) { (void)p;(void)m; }
cairo_pattern_t *cairo_pattern_reference(cairo_pattern_t *p) { if(p) p->ref_count++; return p; }

cairo_font_options_t *cairo_font_options_create(void) { return (cairo_font_options_t*)calloc(1, sizeof(cairo_font_options_t)); }
void cairo_font_options_destroy(cairo_font_options_t *o) { free(o); }
cairo_font_options_t *cairo_font_options_copy(const cairo_font_options_t *o) {
    cairo_font_options_t *n = cairo_font_options_create(); if(o) *n=*o; return n;
}
void cairo_font_options_set_antialias(cairo_font_options_t *o, cairo_antialias_t aa) { o->antialias=aa; }
cairo_antialias_t cairo_font_options_get_antialias(const cairo_font_options_t *o) { return o->antialias; }
void cairo_font_options_set_hint_style(cairo_font_options_t *o, cairo_hint_style_t hs) { o->hint_style=hs; }
cairo_hint_style_t cairo_font_options_get_hint_style(const cairo_font_options_t *o) { return o->hint_style; }
void cairo_font_options_set_hint_metrics(cairo_font_options_t *o, cairo_hint_metrics_t hm) { o->hint_metrics=hm; }
cairo_hint_metrics_t cairo_font_options_get_hint_metrics(const cairo_font_options_t *o) { return o->hint_metrics; }
void cairo_font_options_set_subpixel_order(cairo_font_options_t *o, cairo_subpixel_order_t so) { o->subpixel_order=so; }
void cairo_set_font_options(cairo_t *cr, const cairo_font_options_t *o) { if(o) cr->font_options=*o; }
void cairo_get_font_options(cairo_t *cr, cairo_font_options_t *o) { if(o) *o=cr->font_options; }

cairo_scaled_font_t *cairo_scaled_font_create(cairo_font_face_t *ff, const cairo_matrix_t *fm, const cairo_matrix_t *ctm, const cairo_font_options_t *o) {
    (void)fm;(void)ctm;(void)o;
    cairo_scaled_font_t *sf = (cairo_scaled_font_t*)calloc(1, sizeof(*sf)); sf->face=ff; sf->ref_count=1; return sf;
}
void cairo_scaled_font_destroy(cairo_scaled_font_t *sf) { if(sf && --sf->ref_count<=0) free(sf); }
cairo_scaled_font_t *cairo_scaled_font_reference(cairo_scaled_font_t *sf) { if(sf) sf->ref_count++; return sf; }
void cairo_scaled_font_extents(cairo_scaled_font_t *sf, cairo_font_extents_t *e) { (void)sf; if(e) { e->ascent=12;e->descent=3;e->height=15; } }
void cairo_set_scaled_font(cairo_t *cr, const cairo_scaled_font_t *sf) { (void)cr;(void)sf; }
cairo_scaled_font_t *cairo_get_scaled_font(cairo_t *cr) { return cr->scaled_font; }

static struct _cairo_font_face g_ft_font_face = { .ref_count = 1 };
cairo_font_face_t *cairo_ft_font_face_create_for_ft_face(FT_Face face, int flags) { (void)face;(void)flags; g_ft_font_face.ref_count++; return &g_ft_font_face; }

int cairo_version(void) { return 11600; }
const char *cairo_version_string(void) { return "1.16.0"; }
