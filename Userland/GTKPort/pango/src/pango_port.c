#include "pango.h"
#include <string.h>

extern void *calloc(unsigned long, unsigned long);
extern void *malloc(unsigned long);
extern void  free(void *);
extern char *strdup(const char *);

struct _PangoFontDescription { char family[64]; PangoStyle style; PangoWeight weight; PangoVariant variant; int size; };
struct _PangoContext { PangoFontDescription desc; PangoFontMap *font_map; PangoDirection base_dir; };
struct _PangoLayout {
    PangoContext *context;
    char *text;
    PangoFontDescription *desc;
    int width, height;
    PangoWrapMode wrap;
    PangoEllipsizeMode ellipsize;
    PangoAlignment alignment;
    PangoAttrList *attrs;
    int ref_count;
};
struct _PangoFontMap { int ref_count; };
struct _PangoFontMetrics { int ascent, descent, char_width; int ref_count; };
struct _PangoAttrList { int ref_count; };
struct _PangoLayoutLine { int start_index, length; };
struct _PangoAttribute { int start_index, end_index; };
struct _PangoLanguage { char lang[16]; };

static PangoLanguage g_default_lang = { .lang = "en" };
static PangoFontMap  g_default_fontmap = { .ref_count = 1 };
static PangoLayoutLine g_default_line = { .start_index = 0, .length = 0 };

PangoFontDescription *pango_font_description_new(void) { return (PangoFontDescription*)calloc(1, sizeof(PangoFontDescription)); }
PangoFontDescription *pango_font_description_from_string(const char *str) {
    PangoFontDescription *d = pango_font_description_new();
    if (str) strncpy(d->family, str, 63);
    d->weight = PANGO_WEIGHT_NORMAL; d->size = 12 * PANGO_SCALE;
    return d;
}
PangoFontDescription *pango_font_description_copy(const PangoFontDescription *desc) {
    PangoFontDescription *d = (PangoFontDescription*)malloc(sizeof(*d));
    memcpy(d, desc, sizeof(*d)); return d;
}
void pango_font_description_free(PangoFontDescription *d) { free(d); }
void pango_font_description_set_family(PangoFontDescription *d, const char *f) { if (f) strncpy(d->family, f, 63); }
const char *pango_font_description_get_family(const PangoFontDescription *d) { return d->family; }
void pango_font_description_set_style(PangoFontDescription *d, PangoStyle s) { d->style = s; }
PangoStyle pango_font_description_get_style(const PangoFontDescription *d) { return d->style; }
void pango_font_description_set_weight(PangoFontDescription *d, PangoWeight w) { d->weight = w; }
PangoWeight pango_font_description_get_weight(const PangoFontDescription *d) { return d->weight; }
void pango_font_description_set_size(PangoFontDescription *d, int s) { d->size = s; }
int  pango_font_description_get_size(const PangoFontDescription *d) { return d->size; }
void pango_font_description_set_absolute_size(PangoFontDescription *d, double s) { d->size = (int)(s); }
char *pango_font_description_to_string(const PangoFontDescription *d) { return strdup(d->family[0] ? d->family : "Sans 12"); }
void pango_font_description_merge(PangoFontDescription *d, const PangoFontDescription *m, int replace) {
    if (!m) return;
    if (replace || !d->family[0]) strncpy(d->family, m->family, 63);
    if (replace) { d->style = m->style; d->weight = m->weight; d->size = m->size; }
}

PangoContext *pango_context_new(void) {
    PangoContext *c = (PangoContext*)calloc(1, sizeof(*c));
    strcpy(c->desc.family, "Sans"); c->desc.weight = PANGO_WEIGHT_NORMAL; c->desc.size = 12 * PANGO_SCALE;
    return c;
}
void pango_context_set_font_description(PangoContext *c, const PangoFontDescription *d) { if (d) c->desc = *d; }
PangoFontDescription *pango_context_get_font_description(PangoContext *c) { return &c->desc; }
void pango_context_set_language(PangoContext *c, PangoLanguage *l) { (void)c;(void)l; }
PangoLanguage *pango_context_get_language(PangoContext *c) { (void)c; return &g_default_lang; }
PangoFontMetrics *pango_context_get_metrics(PangoContext *c, const PangoFontDescription *d, PangoLanguage *l) {
    (void)c;(void)d;(void)l;
    PangoFontMetrics *m = (PangoFontMetrics*)calloc(1, sizeof(*m));
    m->ascent = 12 * PANGO_SCALE; m->descent = 3 * PANGO_SCALE; m->char_width = 8 * PANGO_SCALE; m->ref_count = 1;
    return m;
}
PangoDirection pango_context_get_base_dir(PangoContext *c) { return c->base_dir; }
void pango_context_set_base_dir(PangoContext *c, PangoDirection d) { c->base_dir = d; }
void pango_context_set_font_map(PangoContext *c, PangoFontMap *fm) { c->font_map = fm; }
PangoFontMap *pango_context_get_font_map(PangoContext *c) { return c->font_map ? c->font_map : &g_default_fontmap; }

PangoLayout *pango_layout_new(PangoContext *ctx) {
    PangoLayout *l = (PangoLayout*)calloc(1, sizeof(*l));
    l->context = ctx; l->width = -1; l->ref_count = 1;
    return l;
}
PangoLayout *pango_layout_copy(PangoLayout *src) {
    PangoLayout *l = (PangoLayout*)calloc(1, sizeof(*l));
    *l = *src; l->text = src->text ? strdup(src->text) : NULL; l->ref_count = 1;
    return l;
}
void pango_layout_set_text(PangoLayout *l, const char *t, int len) {
    free(l->text);
    if (!t) { l->text = NULL; return; }
    int n = (len < 0) ? (int)strlen(t) : len;
    l->text = (char*)malloc((unsigned long)n + 1);
    memcpy(l->text, t, (unsigned long)n); l->text[n] = 0;
}
const char *pango_layout_get_text(PangoLayout *l) { return l->text ? l->text : ""; }
void pango_layout_set_markup(PangoLayout *l, const char *m, int len) { pango_layout_set_text(l, m, len); }
void pango_layout_set_font_description(PangoLayout *l, const PangoFontDescription *d) { (void)l;(void)d; }
const PangoFontDescription *pango_layout_get_font_description(PangoLayout *l) { return l->desc ? l->desc : &l->context->desc; }
void pango_layout_set_width(PangoLayout *l, int w) { l->width = w; }
int  pango_layout_get_width(PangoLayout *l) { return l->width; }
void pango_layout_set_height(PangoLayout *l, int h) { l->height = h; }
void pango_layout_set_wrap(PangoLayout *l, PangoWrapMode w) { l->wrap = w; }
void pango_layout_set_ellipsize(PangoLayout *l, PangoEllipsizeMode e) { l->ellipsize = e; }
void pango_layout_set_alignment(PangoLayout *l, PangoAlignment a) { l->alignment = a; }
void pango_layout_set_single_paragraph_mode(PangoLayout *l, int s) { (void)l;(void)s; }

void pango_layout_get_size(PangoLayout *l, int *w, int *h) {
    int tlen = l->text ? (int)strlen(l->text) : 0;
    if (w) *w = tlen * 8 * PANGO_SCALE;
    if (h) *h = 16 * PANGO_SCALE;
}
void pango_layout_get_pixel_size(PangoLayout *l, int *w, int *h) {
    int sw, sh;
    pango_layout_get_size(l, &sw, &sh);
    if (w) *w = sw / PANGO_SCALE;
    if (h) *h = sh / PANGO_SCALE;
}
void pango_layout_get_extents(PangoLayout *l, PangoRectangle *ink, PangoRectangle *logical) {
    int w, h; pango_layout_get_size(l, &w, &h);
    if (ink)     { ink->x=0; ink->y=0; ink->width=w; ink->height=h; }
    if (logical) { logical->x=0; logical->y=0; logical->width=w; logical->height=h; }
}
void pango_layout_get_pixel_extents(PangoLayout *l, PangoRectangle *ink, PangoRectangle *logical) {
    int w, h; pango_layout_get_pixel_size(l, &w, &h);
    if (ink)     { ink->x=0; ink->y=0; ink->width=w; ink->height=h; }
    if (logical) { logical->x=0; logical->y=0; logical->width=w; logical->height=h; }
}
int pango_layout_get_line_count(PangoLayout *l) { (void)l; return 1; }
PangoLayoutLine *pango_layout_get_line(PangoLayout *l, int line) { (void)l;(void)line; return &g_default_line; }
PangoLayoutLine *pango_layout_get_line_readonly(PangoLayout *l, int line) { return pango_layout_get_line(l, line); }
PangoContext *pango_layout_get_context(PangoLayout *l) { return l->context; }
void pango_layout_set_attributes(PangoLayout *l, PangoAttrList *a) { l->attrs = a; }
void pango_layout_set_indent(PangoLayout *l, int i) { (void)l;(void)i; }
void pango_layout_set_spacing(PangoLayout *l, int s) { (void)l;(void)s; }

PangoAttrList *pango_attr_list_new(void) { PangoAttrList *a = (PangoAttrList*)calloc(1, sizeof(*a)); a->ref_count = 1; return a; }
void pango_attr_list_unref(PangoAttrList *a) { if (a && --a->ref_count <= 0) free(a); }
void pango_attr_list_insert(PangoAttrList *a, PangoAttribute *attr) { (void)a; free(attr); }
PangoAttribute *pango_attr_foreground_new(unsigned short r, unsigned short g, unsigned short b) { (void)r;(void)g;(void)b; return (PangoAttribute*)calloc(1, sizeof(PangoAttribute)); }
PangoAttribute *pango_attr_weight_new(PangoWeight w) { (void)w; return (PangoAttribute*)calloc(1, sizeof(PangoAttribute)); }
PangoAttribute *pango_attr_size_new(int s) { (void)s; return (PangoAttribute*)calloc(1, sizeof(PangoAttribute)); }
PangoAttribute *pango_attr_family_new(const char *f) { (void)f; return (PangoAttribute*)calloc(1, sizeof(PangoAttribute)); }

PangoLanguage *pango_language_from_string(const char *l) { (void)l; return &g_default_lang; }
PangoLanguage *pango_language_get_default(void) { return &g_default_lang; }
const char *pango_language_to_string(PangoLanguage *l) { return l->lang; }

int pango_font_metrics_get_ascent(PangoFontMetrics *m) { return m->ascent; }
int pango_font_metrics_get_descent(PangoFontMetrics *m) { return m->descent; }
int pango_font_metrics_get_approximate_char_width(PangoFontMetrics *m) { return m->char_width; }
void pango_font_metrics_unref(PangoFontMetrics *m) { if (m && --m->ref_count <= 0) free(m); }

PangoLayout *pango_cairo_create_layout(cairo_t *cr) { (void)cr; return pango_layout_new(pango_context_new()); }
void pango_cairo_show_layout(cairo_t *cr, PangoLayout *l) { (void)cr;(void)l; }
void pango_cairo_update_layout(cairo_t *cr, PangoLayout *l) { (void)cr;(void)l; }
PangoContext *pango_cairo_create_context(cairo_t *cr) { (void)cr; return pango_context_new(); }
void pango_cairo_context_set_resolution(PangoContext *c, double dpi) { (void)c;(void)dpi; }
PangoFontMap *pango_cairo_font_map_get_default(void) { return &g_default_fontmap; }
PangoFontMap *pango_cairo_font_map_new(void) { PangoFontMap *m = (PangoFontMap*)calloc(1, sizeof(*m)); m->ref_count = 1; return m; }
PangoContext *pango_font_map_create_context(PangoFontMap *fm) { (void)fm; return pango_context_new(); }

GType pango_layout_get_type(void) { return 100; }
GType pango_font_description_get_type(void) { return 101; }
GType pango_context_get_type(void) { return 102; }
