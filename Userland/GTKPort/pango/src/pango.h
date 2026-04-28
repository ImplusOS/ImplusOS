#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct _PangoContext      PangoContext;
typedef struct _PangoLayout       PangoLayout;
typedef struct _PangoFontDescription PangoFontDescription;
typedef struct _PangoFontMap      PangoFontMap;
typedef struct _PangoFont         PangoFont;
typedef struct _PangoFontset      PangoFontset;
typedef struct _PangoFontFamily   PangoFontFamily;
typedef struct _PangoFontFace     PangoFontFace;
typedef struct _PangoAttrList     PangoAttrList;
typedef struct _PangoAttribute    PangoAttribute;
typedef struct _PangoLayoutLine   PangoLayoutLine;
typedef struct _PangoLayoutIter   PangoLayoutIter;
typedef struct _PangoTabArray     PangoTabArray;
typedef struct _PangoLanguage     PangoLanguage;
typedef struct _PangoFontMetrics  PangoFontMetrics;

typedef unsigned int PangoGlyph;

typedef enum { PANGO_STYLE_NORMAL=0, PANGO_STYLE_OBLIQUE, PANGO_STYLE_ITALIC } PangoStyle;
typedef enum { PANGO_WEIGHT_THIN=100, PANGO_WEIGHT_LIGHT=300, PANGO_WEIGHT_NORMAL=400,
               PANGO_WEIGHT_SEMIBOLD=600, PANGO_WEIGHT_BOLD=700, PANGO_WEIGHT_HEAVY=900 } PangoWeight;
typedef enum { PANGO_VARIANT_NORMAL=0, PANGO_VARIANT_SMALL_CAPS } PangoVariant;
typedef enum { PANGO_STRETCH_ULTRA_CONDENSED=0, PANGO_STRETCH_NORMAL=4, PANGO_STRETCH_ULTRA_EXPANDED=8 } PangoStretch;
typedef enum { PANGO_ALIGN_LEFT=0, PANGO_ALIGN_CENTER, PANGO_ALIGN_RIGHT } PangoAlignment;
typedef enum { PANGO_WRAP_WORD=0, PANGO_WRAP_CHAR, PANGO_WRAP_WORD_CHAR } PangoWrapMode;
typedef enum { PANGO_ELLIPSIZE_NONE=0, PANGO_ELLIPSIZE_START, PANGO_ELLIPSIZE_MIDDLE, PANGO_ELLIPSIZE_END } PangoEllipsizeMode;
typedef enum { PANGO_DIRECTION_LTR=0, PANGO_DIRECTION_RTL } PangoDirection;
typedef enum { PANGO_GRAVITY_SOUTH=0, PANGO_GRAVITY_EAST, PANGO_GRAVITY_NORTH, PANGO_GRAVITY_WEST, PANGO_GRAVITY_AUTO } PangoGravity;

#define PANGO_SCALE 1024

typedef struct { int x, y, width, height; } PangoRectangle;

PangoFontDescription *pango_font_description_new(void);
PangoFontDescription *pango_font_description_from_string(const char *str);
PangoFontDescription *pango_font_description_copy(const PangoFontDescription *desc);
void    pango_font_description_free(PangoFontDescription *desc);
void    pango_font_description_set_family(PangoFontDescription *desc, const char *family);
const char *pango_font_description_get_family(const PangoFontDescription *desc);
void    pango_font_description_set_style(PangoFontDescription *desc, PangoStyle style);
PangoStyle pango_font_description_get_style(const PangoFontDescription *desc);
void    pango_font_description_set_weight(PangoFontDescription *desc, PangoWeight weight);
PangoWeight pango_font_description_get_weight(const PangoFontDescription *desc);
void    pango_font_description_set_size(PangoFontDescription *desc, int size);
int     pango_font_description_get_size(const PangoFontDescription *desc);
void    pango_font_description_set_absolute_size(PangoFontDescription *desc, double size);
char   *pango_font_description_to_string(const PangoFontDescription *desc);
void    pango_font_description_merge(PangoFontDescription *desc, const PangoFontDescription *desc_to_merge, int replace_existing);

PangoContext *pango_context_new(void);
void          pango_context_set_font_description(PangoContext *context, const PangoFontDescription *desc);
PangoFontDescription *pango_context_get_font_description(PangoContext *context);
void          pango_context_set_language(PangoContext *context, PangoLanguage *language);
PangoLanguage *pango_context_get_language(PangoContext *context);
PangoFontMetrics *pango_context_get_metrics(PangoContext *context, const PangoFontDescription *desc, PangoLanguage *language);
PangoDirection pango_context_get_base_dir(PangoContext *context);
void          pango_context_set_base_dir(PangoContext *context, PangoDirection direction);
void          pango_context_set_font_map(PangoContext *context, PangoFontMap *font_map);
PangoFontMap *pango_context_get_font_map(PangoContext *context);

PangoLayout *pango_layout_new(PangoContext *context);
PangoLayout *pango_layout_copy(PangoLayout *src);
void         pango_layout_set_text(PangoLayout *layout, const char *text, int length);
const char  *pango_layout_get_text(PangoLayout *layout);
void         pango_layout_set_markup(PangoLayout *layout, const char *markup, int length);
void         pango_layout_set_font_description(PangoLayout *layout, const PangoFontDescription *desc);
const PangoFontDescription *pango_layout_get_font_description(PangoLayout *layout);
void         pango_layout_set_width(PangoLayout *layout, int width);
int          pango_layout_get_width(PangoLayout *layout);
void         pango_layout_set_height(PangoLayout *layout, int height);
void         pango_layout_set_wrap(PangoLayout *layout, PangoWrapMode wrap);
void         pango_layout_set_ellipsize(PangoLayout *layout, PangoEllipsizeMode ellipsize);
void         pango_layout_set_alignment(PangoLayout *layout, PangoAlignment alignment);
void         pango_layout_set_single_paragraph_mode(PangoLayout *layout, int setting);
void         pango_layout_get_size(PangoLayout *layout, int *width, int *height);
void         pango_layout_get_pixel_size(PangoLayout *layout, int *width, int *height);
void         pango_layout_get_extents(PangoLayout *layout, PangoRectangle *ink, PangoRectangle *logical);
void         pango_layout_get_pixel_extents(PangoLayout *layout, PangoRectangle *ink, PangoRectangle *logical);
int          pango_layout_get_line_count(PangoLayout *layout);
PangoLayoutLine *pango_layout_get_line(PangoLayout *layout, int line);
PangoLayoutLine *pango_layout_get_line_readonly(PangoLayout *layout, int line);
PangoContext    *pango_layout_get_context(PangoLayout *layout);
void         pango_layout_set_attributes(PangoLayout *layout, PangoAttrList *attrs);
void         pango_layout_set_indent(PangoLayout *layout, int indent);
void         pango_layout_set_spacing(PangoLayout *layout, int spacing);

PangoAttrList *pango_attr_list_new(void);
void           pango_attr_list_unref(PangoAttrList *list);
void           pango_attr_list_insert(PangoAttrList *list, PangoAttribute *attr);
PangoAttribute *pango_attr_foreground_new(unsigned short r, unsigned short g, unsigned short b);
PangoAttribute *pango_attr_weight_new(PangoWeight weight);
PangoAttribute *pango_attr_size_new(int size);
PangoAttribute *pango_attr_family_new(const char *family);

PangoLanguage *pango_language_from_string(const char *language);
PangoLanguage *pango_language_get_default(void);
const char    *pango_language_to_string(PangoLanguage *language);

int pango_font_metrics_get_ascent(PangoFontMetrics *metrics);
int pango_font_metrics_get_descent(PangoFontMetrics *metrics);
int pango_font_metrics_get_approximate_char_width(PangoFontMetrics *metrics);
void pango_font_metrics_unref(PangoFontMetrics *metrics);

typedef struct _cairo cairo_t;
PangoLayout  *pango_cairo_create_layout(cairo_t *cr);
void          pango_cairo_show_layout(cairo_t *cr, PangoLayout *layout);
void          pango_cairo_update_layout(cairo_t *cr, PangoLayout *layout);
PangoContext *pango_cairo_create_context(cairo_t *cr);
void          pango_cairo_context_set_resolution(PangoContext *context, double dpi);
PangoFontMap *pango_cairo_font_map_get_default(void);
PangoFontMap *pango_cairo_font_map_new(void);
PangoContext *pango_font_map_create_context(PangoFontMap *fontmap);

typedef unsigned long GType;
GType pango_layout_get_type(void);
GType pango_font_description_get_type(void);
GType pango_context_get_type(void);
