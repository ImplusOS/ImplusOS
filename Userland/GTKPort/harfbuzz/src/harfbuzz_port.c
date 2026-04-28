#include "hb.h"
#include <string.h>

extern void *calloc(unsigned long, unsigned long);
extern void *malloc(unsigned long);
extern void *realloc(void *, unsigned long);
extern void  free(void *);

struct hb_blob_t   { const char *data; unsigned int length; hb_memory_mode_t mode; void *user_data; hb_destroy_func_t destroy; };
struct hb_face_t   { hb_blob_t *blob; unsigned int index; unsigned int upem; };
struct hb_font_t   { hb_face_t *face; int x_scale, y_scale; unsigned int x_ppem, y_ppem; };
struct hb_buffer_t {
    hb_glyph_info_t     *info;
    hb_glyph_position_t *pos;
    unsigned int          len, alloc;
    hb_direction_t        direction;
    hb_script_t           script;
    hb_language_t         language;
};

static const char default_lang[] = "en";

hb_blob_t *hb_blob_create(const char *data, unsigned int length, hb_memory_mode_t mode, void *ud, hb_destroy_func_t destroy) {
    hb_blob_t *b = (hb_blob_t*)calloc(1, sizeof(*b));
    b->data = data; b->length = length; b->mode = mode; b->user_data = ud; b->destroy = destroy;
    return b;
}
void hb_blob_destroy(hb_blob_t *b) { if (b) { if (b->destroy) b->destroy(b->user_data); free(b); } }

hb_face_t *hb_face_create(hb_blob_t *blob, unsigned int index) {
    hb_face_t *f = (hb_face_t*)calloc(1, sizeof(*f));
    f->blob = blob; f->index = index; f->upem = 1000;
    return f;
}
void hb_face_destroy(hb_face_t *f) { free(f); }
void hb_face_set_upem(hb_face_t *f, unsigned int upem) { f->upem = upem; }
unsigned hb_face_get_upem(hb_face_t *f) { return f ? f->upem : 1000; }

hb_font_t *hb_font_create(hb_face_t *face) {
    hb_font_t *f = (hb_font_t*)calloc(1, sizeof(*f));
    f->face = face; f->x_scale = f->y_scale = 1000;
    return f;
}
void hb_font_destroy(hb_font_t *f) { free(f); }
void hb_font_set_scale(hb_font_t *f, int xs, int ys) { f->x_scale = xs; f->y_scale = ys; }
void hb_font_set_ppem(hb_font_t *f, unsigned int xp, unsigned int yp) { f->x_ppem = xp; f->y_ppem = yp; }
hb_face_t *hb_font_get_face(hb_font_t *f) { return f ? f->face : NULL; }

hb_buffer_t *hb_buffer_create(void) { return (hb_buffer_t*)calloc(1, sizeof(hb_buffer_t)); }
void hb_buffer_destroy(hb_buffer_t *b) { if (b) { free(b->info); free(b->pos); free(b); } }
void hb_buffer_reset(hb_buffer_t *b) { b->len = 0; }

static void buf_ensure(hb_buffer_t *b, unsigned int need) {
    if (b->alloc >= need) return;
    unsigned int n = need * 2;
    b->info = (hb_glyph_info_t*)realloc(b->info, n * sizeof(*b->info));
    b->pos  = (hb_glyph_position_t*)realloc(b->pos, n * sizeof(*b->pos));
    b->alloc = n;
}

void hb_buffer_add_utf8(hb_buffer_t *b, const char *text, int text_length,
                        unsigned int item_offset, int item_length) {
    (void)item_offset;
    unsigned int tlen = (text_length < 0) ? (unsigned int)strlen(text) : (unsigned int)text_length;
    unsigned int ilen = (item_length < 0) ? tlen : (unsigned int)item_length;
    buf_ensure(b, b->len + ilen);
    for (unsigned int i = 0; i < ilen && i < tlen; i++) {
        b->info[b->len].codepoint = (uint8_t)text[i];
        b->info[b->len].cluster = i;
        memset(&b->pos[b->len], 0, sizeof(b->pos[0]));
        b->len++;
    }
}

void hb_buffer_add_utf32(hb_buffer_t *b, const uint32_t *text, int text_length,
                         unsigned int item_offset, int item_length) {
    (void)item_offset;
    unsigned int tlen = (text_length < 0) ? 0 : (unsigned int)text_length;
    unsigned int ilen = (item_length < 0) ? tlen : (unsigned int)item_length;
    buf_ensure(b, b->len + ilen);
    for (unsigned int i = 0; i < ilen && i < tlen; i++) {
        b->info[b->len].codepoint = text[i];
        b->info[b->len].cluster = i;
        memset(&b->pos[b->len], 0, sizeof(b->pos[0]));
        b->len++;
    }
}

void hb_buffer_set_direction(hb_buffer_t *b, hb_direction_t d) { b->direction = d; }
void hb_buffer_set_script(hb_buffer_t *b, hb_script_t s)       { b->script = s; }
void hb_buffer_set_language(hb_buffer_t *b, hb_language_t l)    { b->language = l; }
void hb_buffer_guess_segment_properties(hb_buffer_t *b) {
    if (!b->direction) b->direction = HB_DIRECTION_LTR;
    if (!b->script) b->script = HB_SCRIPT_LATIN;
    if (!b->language) b->language = (hb_language_t)default_lang;
}

unsigned int hb_buffer_get_length(hb_buffer_t *b) { return b->len; }
hb_glyph_info_t     *hb_buffer_get_glyph_infos(hb_buffer_t *b, unsigned int *l)    { if (l) *l = b->len; return b->info; }
hb_glyph_position_t *hb_buffer_get_glyph_positions(hb_buffer_t *b, unsigned int *l) { if (l) *l = b->len; return b->pos; }

void hb_shape(hb_font_t *font, hb_buffer_t *buf, const hb_feature_t *features, unsigned int nf) {
    (void)font; (void)features; (void)nf;
    for (unsigned int i = 0; i < buf->len; i++) {
        buf->pos[i].x_advance = 512;
        buf->pos[i].y_advance = 0;
        buf->pos[i].x_offset = 0;
        buf->pos[i].y_offset = 0;
    }
}

hb_language_t hb_language_from_string(const char *str, int len) { (void)str;(void)len; return (hb_language_t)default_lang; }
const char   *hb_language_to_string(hb_language_t l) { return l ? (const char*)l : "en"; }
hb_language_t hb_language_get_default(void) { return (hb_language_t)default_lang; }

hb_direction_t hb_direction_from_string(const char *str, int len) { (void)str;(void)len; return HB_DIRECTION_LTR; }
hb_script_t    hb_script_from_string(const char *str, int len) { (void)str;(void)len; return HB_SCRIPT_LATIN; }
hb_direction_t hb_script_get_horizontal_direction(hb_script_t s) { (void)s; return HB_DIRECTION_LTR; }

hb_font_t *hb_ft_font_create(FT_Face ft_face, hb_destroy_func_t d) { (void)ft_face;(void)d; return hb_font_create(NULL); }
hb_face_t *hb_ft_face_create(FT_Face ft_face, hb_destroy_func_t d) { (void)ft_face;(void)d; return hb_face_create(NULL, 0); }
