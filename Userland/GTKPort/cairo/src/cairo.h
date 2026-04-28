#pragma once

#include <stddef.h>
#include <stdint.h>

typedef int cairo_bool_t;
typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;
typedef struct _cairo_pattern cairo_pattern_t;
typedef struct _cairo_font_face cairo_font_face_t;
typedef struct _cairo_scaled_font cairo_scaled_font_t;
typedef struct _cairo_font_options cairo_font_options_t;
typedef struct _cairo_matrix { double xx, yx, xy, yy, x0, y0; } cairo_matrix_t;
typedef struct _cairo_text_extents { double x_bearing, y_bearing, width, height, x_advance, y_advance; } cairo_text_extents_t;
typedef struct _cairo_font_extents { double ascent, descent, height, max_x_advance, max_y_advance; } cairo_font_extents_t;
typedef struct _cairo_glyph { unsigned long index; double x, y; } cairo_glyph_t;

typedef enum { CAIRO_STATUS_SUCCESS=0, CAIRO_STATUS_NO_MEMORY, CAIRO_STATUS_INVALID_RESTORE,
               CAIRO_STATUS_NULL_POINTER, CAIRO_STATUS_SURFACE_FINISHED } cairo_status_t;
typedef enum { CAIRO_FORMAT_INVALID=-1, CAIRO_FORMAT_ARGB32=0, CAIRO_FORMAT_RGB24, CAIRO_FORMAT_A8, CAIRO_FORMAT_A1,
               CAIRO_FORMAT_RGB16_565, CAIRO_FORMAT_RGB30 } cairo_format_t;
typedef enum { CAIRO_OPERATOR_CLEAR=0, CAIRO_OPERATOR_SOURCE, CAIRO_OPERATOR_OVER } cairo_operator_t;
typedef enum { CAIRO_ANTIALIAS_DEFAULT=0, CAIRO_ANTIALIAS_NONE, CAIRO_ANTIALIAS_GRAY, CAIRO_ANTIALIAS_SUBPIXEL,
               CAIRO_ANTIALIAS_FAST, CAIRO_ANTIALIAS_GOOD, CAIRO_ANTIALIAS_BEST } cairo_antialias_t;
typedef enum { CAIRO_LINE_CAP_BUTT=0, CAIRO_LINE_CAP_ROUND, CAIRO_LINE_CAP_SQUARE } cairo_line_cap_t;
typedef enum { CAIRO_LINE_JOIN_MITER=0, CAIRO_LINE_JOIN_ROUND, CAIRO_LINE_JOIN_BEVEL } cairo_line_join_t;
typedef enum { CAIRO_FILL_RULE_WINDING=0, CAIRO_FILL_RULE_EVEN_ODD } cairo_fill_rule_t;
typedef enum { CAIRO_FONT_SLANT_NORMAL=0, CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_SLANT_OBLIQUE } cairo_font_slant_t;
typedef enum { CAIRO_FONT_WEIGHT_NORMAL=0, CAIRO_FONT_WEIGHT_BOLD } cairo_font_weight_t;
typedef enum { CAIRO_HINT_STYLE_DEFAULT=0, CAIRO_HINT_STYLE_NONE, CAIRO_HINT_STYLE_SLIGHT,
               CAIRO_HINT_STYLE_MEDIUM, CAIRO_HINT_STYLE_FULL } cairo_hint_style_t;
typedef enum { CAIRO_HINT_METRICS_DEFAULT=0, CAIRO_HINT_METRICS_OFF, CAIRO_HINT_METRICS_ON } cairo_hint_metrics_t;
typedef enum { CAIRO_SUBPIXEL_ORDER_DEFAULT=0, CAIRO_SUBPIXEL_ORDER_RGB, CAIRO_SUBPIXEL_ORDER_BGR,
               CAIRO_SUBPIXEL_ORDER_VRGB, CAIRO_SUBPIXEL_ORDER_VBGR } cairo_subpixel_order_t;
typedef enum { CAIRO_CONTENT_COLOR=0x1000, CAIRO_CONTENT_ALPHA=0x2000, CAIRO_CONTENT_COLOR_ALPHA=0x3000 } cairo_content_t;
typedef enum { CAIRO_EXTEND_NONE=0, CAIRO_EXTEND_REPEAT, CAIRO_EXTEND_REFLECT, CAIRO_EXTEND_PAD } cairo_extend_t;
typedef enum { CAIRO_FILTER_FAST=0, CAIRO_FILTER_GOOD, CAIRO_FILTER_BEST, CAIRO_FILTER_NEAREST,
               CAIRO_FILTER_BILINEAR, CAIRO_FILTER_GAUSSIAN } cairo_filter_t;
typedef enum { CAIRO_FONT_TYPE_TOY=0, CAIRO_FONT_TYPE_FT, CAIRO_FONT_TYPE_WIN32, CAIRO_FONT_TYPE_QUARTZ, CAIRO_FONT_TYPE_USER } cairo_font_type_t;
typedef enum { CAIRO_SURFACE_TYPE_IMAGE=0, CAIRO_SURFACE_TYPE_XLIB=1 } cairo_surface_type_t;

typedef void (*cairo_destroy_func_t)(void *data);

cairo_surface_t *cairo_image_surface_create(cairo_format_t format, int width, int height);
cairo_surface_t *cairo_image_surface_create_for_data(unsigned char *data, cairo_format_t format, int width, int height, int stride);
unsigned char   *cairo_image_surface_get_data(cairo_surface_t *surface);
int              cairo_image_surface_get_width(cairo_surface_t *surface);
int              cairo_image_surface_get_height(cairo_surface_t *surface);
int              cairo_image_surface_get_stride(cairo_surface_t *surface);
cairo_format_t   cairo_image_surface_get_format(cairo_surface_t *surface);
cairo_surface_t *cairo_surface_reference(cairo_surface_t *surface);
void             cairo_surface_destroy(cairo_surface_t *surface);
void             cairo_surface_flush(cairo_surface_t *surface);
void             cairo_surface_mark_dirty(cairo_surface_t *surface);
cairo_status_t   cairo_surface_status(cairo_surface_t *surface);
int              cairo_format_stride_for_width(cairo_format_t format, int width);
cairo_surface_t *cairo_surface_create_similar(cairo_surface_t *other, cairo_content_t content, int w, int h);
cairo_surface_t *cairo_surface_create_similar_image(cairo_surface_t *other, cairo_format_t format, int w, int h);
void             cairo_surface_set_device_offset(cairo_surface_t *s, double x, double y);

cairo_t        *cairo_create(cairo_surface_t *target);
void            cairo_destroy(cairo_t *cr);
cairo_t        *cairo_reference(cairo_t *cr);
cairo_status_t  cairo_status(cairo_t *cr);
cairo_surface_t *cairo_get_target(cairo_t *cr);
void            cairo_save(cairo_t *cr);
void            cairo_restore(cairo_t *cr);
void            cairo_set_operator(cairo_t *cr, cairo_operator_t op);
void            cairo_set_source_rgb(cairo_t *cr, double r, double g, double b);
void            cairo_set_source_rgba(cairo_t *cr, double r, double g, double b, double a);
void            cairo_set_source_surface(cairo_t *cr, cairo_surface_t *surface, double x, double y);
void            cairo_set_source(cairo_t *cr, cairo_pattern_t *source);
cairo_pattern_t *cairo_get_source(cairo_t *cr);
void            cairo_set_line_width(cairo_t *cr, double width);
double          cairo_get_line_width(cairo_t *cr);
void            cairo_set_line_cap(cairo_t *cr, cairo_line_cap_t cap);
void            cairo_set_line_join(cairo_t *cr, cairo_line_join_t join);
void            cairo_set_antialias(cairo_t *cr, cairo_antialias_t aa);
void            cairo_set_fill_rule(cairo_t *cr, cairo_fill_rule_t rule);

void cairo_new_path(cairo_t *cr);
void cairo_new_sub_path(cairo_t *cr);
void cairo_close_path(cairo_t *cr);
void cairo_move_to(cairo_t *cr, double x, double y);
void cairo_line_to(cairo_t *cr, double x, double y);
void cairo_curve_to(cairo_t *cr, double x1, double y1, double x2, double y2, double x3, double y3);
void cairo_arc(cairo_t *cr, double xc, double yc, double r, double a1, double a2);
void cairo_rectangle(cairo_t *cr, double x, double y, double w, double h);

void cairo_paint(cairo_t *cr);
void cairo_paint_with_alpha(cairo_t *cr, double alpha);
void cairo_fill(cairo_t *cr);
void cairo_fill_preserve(cairo_t *cr);
void cairo_stroke(cairo_t *cr);
void cairo_stroke_preserve(cairo_t *cr);
void cairo_clip(cairo_t *cr);
void cairo_clip_preserve(cairo_t *cr);
void cairo_reset_clip(cairo_t *cr);
void cairo_mask(cairo_t *cr, cairo_pattern_t *pattern);
void cairo_mask_surface(cairo_t *cr, cairo_surface_t *surface, double x, double y);

void cairo_translate(cairo_t *cr, double tx, double ty);
void cairo_scale(cairo_t *cr, double sx, double sy);
void cairo_rotate(cairo_t *cr, double angle);
void cairo_transform(cairo_t *cr, const cairo_matrix_t *matrix);
void cairo_set_matrix(cairo_t *cr, const cairo_matrix_t *matrix);
void cairo_get_matrix(cairo_t *cr, cairo_matrix_t *matrix);
void cairo_identity_matrix(cairo_t *cr);

void cairo_matrix_init(cairo_matrix_t *m, double xx, double yx, double xy, double yy, double x0, double y0);
void cairo_matrix_init_identity(cairo_matrix_t *m);
void cairo_matrix_init_translate(cairo_matrix_t *m, double tx, double ty);
void cairo_matrix_init_scale(cairo_matrix_t *m, double sx, double sy);
void cairo_matrix_multiply(cairo_matrix_t *r, const cairo_matrix_t *a, const cairo_matrix_t *b);
cairo_status_t cairo_matrix_invert(cairo_matrix_t *m);
void cairo_matrix_translate(cairo_matrix_t *m, double tx, double ty);
void cairo_matrix_scale(cairo_matrix_t *m, double sx, double sy);

void cairo_select_font_face(cairo_t *cr, const char *family, cairo_font_slant_t slant, cairo_font_weight_t weight);
void cairo_set_font_size(cairo_t *cr, double size);
void cairo_show_text(cairo_t *cr, const char *utf8);
void cairo_text_extents(cairo_t *cr, const char *utf8, cairo_text_extents_t *extents);
void cairo_font_extents(cairo_t *cr, cairo_font_extents_t *extents);
void cairo_show_glyphs(cairo_t *cr, const cairo_glyph_t *glyphs, int num_glyphs);

cairo_pattern_t *cairo_pattern_create_rgb(double r, double g, double b);
cairo_pattern_t *cairo_pattern_create_rgba(double r, double g, double b, double a);
cairo_pattern_t *cairo_pattern_create_for_surface(cairo_surface_t *surface);
cairo_pattern_t *cairo_pattern_create_linear(double x0, double y0, double x1, double y1);
cairo_pattern_t *cairo_pattern_create_radial(double cx0, double cy0, double r0, double cx1, double cy1, double r1);
void             cairo_pattern_destroy(cairo_pattern_t *pattern);
void             cairo_pattern_add_color_stop_rgb(cairo_pattern_t *p, double offset, double r, double g, double b);
void             cairo_pattern_add_color_stop_rgba(cairo_pattern_t *p, double offset, double r, double g, double b, double a);
void             cairo_pattern_set_extend(cairo_pattern_t *p, cairo_extend_t extend);
void             cairo_pattern_set_filter(cairo_pattern_t *p, cairo_filter_t filter);
void             cairo_pattern_set_matrix(cairo_pattern_t *p, const cairo_matrix_t *m);
cairo_pattern_t *cairo_pattern_reference(cairo_pattern_t *p);

cairo_font_options_t *cairo_font_options_create(void);
void                  cairo_font_options_destroy(cairo_font_options_t *options);
cairo_font_options_t *cairo_font_options_copy(const cairo_font_options_t *original);
void                  cairo_font_options_set_antialias(cairo_font_options_t *o, cairo_antialias_t aa);
cairo_antialias_t     cairo_font_options_get_antialias(const cairo_font_options_t *o);
void                  cairo_font_options_set_hint_style(cairo_font_options_t *o, cairo_hint_style_t hs);
cairo_hint_style_t    cairo_font_options_get_hint_style(const cairo_font_options_t *o);
void                  cairo_font_options_set_hint_metrics(cairo_font_options_t *o, cairo_hint_metrics_t hm);
cairo_hint_metrics_t  cairo_font_options_get_hint_metrics(const cairo_font_options_t *o);
void                  cairo_font_options_set_subpixel_order(cairo_font_options_t *o, cairo_subpixel_order_t so);
void                  cairo_set_font_options(cairo_t *cr, const cairo_font_options_t *o);
void                  cairo_get_font_options(cairo_t *cr, cairo_font_options_t *o);

cairo_scaled_font_t *cairo_scaled_font_create(cairo_font_face_t *ff, const cairo_matrix_t *fm, const cairo_matrix_t *ctm, const cairo_font_options_t *o);
void                 cairo_scaled_font_destroy(cairo_scaled_font_t *sf);
cairo_scaled_font_t *cairo_scaled_font_reference(cairo_scaled_font_t *sf);
void                 cairo_scaled_font_extents(cairo_scaled_font_t *sf, cairo_font_extents_t *e);
void                 cairo_set_scaled_font(cairo_t *cr, const cairo_scaled_font_t *sf);
cairo_scaled_font_t *cairo_get_scaled_font(cairo_t *cr);

typedef struct FT_FaceRec_ *FT_Face;
cairo_font_face_t *cairo_ft_font_face_create_for_ft_face(FT_Face face, int load_flags);

int cairo_version(void);
const char *cairo_version_string(void);
