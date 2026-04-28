#pragma once

#include <stddef.h>
#include <stdint.h>

typedef int      hb_bool_t;
typedef uint32_t hb_codepoint_t;
typedef int32_t  hb_position_t;
typedef uint32_t hb_mask_t;
typedef uint32_t hb_tag_t;

#define HB_TAG(a,b,c,d) ((hb_tag_t)((((uint32_t)(a))<<24)|(((uint32_t)(b))<<16)|(((uint32_t)(c))<<8)|((uint32_t)(d))))

typedef enum { HB_DIRECTION_INVALID=0, HB_DIRECTION_LTR=4, HB_DIRECTION_RTL, HB_DIRECTION_TTB, HB_DIRECTION_BTT } hb_direction_t;
typedef enum { HB_SCRIPT_COMMON=0, HB_SCRIPT_LATIN=1, HB_SCRIPT_UNKNOWN=0x7FFFFFFF } hb_script_t;

typedef struct hb_language_impl_t *hb_language_t;
typedef struct hb_blob_t    hb_blob_t;
typedef struct hb_face_t    hb_face_t;
typedef struct hb_font_t    hb_font_t;
typedef struct hb_buffer_t  hb_buffer_t;
typedef struct hb_feature_t { hb_tag_t tag; uint32_t value; unsigned int start, end; } hb_feature_t;

typedef struct hb_glyph_info_t { hb_codepoint_t codepoint; uint32_t cluster; } hb_glyph_info_t;
typedef struct hb_glyph_position_t { hb_position_t x_advance, y_advance, x_offset, y_offset; } hb_glyph_position_t;

typedef enum { HB_MEMORY_MODE_DUPLICATE=0, HB_MEMORY_MODE_READONLY, HB_MEMORY_MODE_WRITABLE, HB_MEMORY_MODE_READONLY_MAY_MAKE_WRITABLE } hb_memory_mode_t;
typedef void (*hb_destroy_func_t)(void *user_data);

hb_blob_t *hb_blob_create(const char *data, unsigned int length, hb_memory_mode_t mode, void *user_data, hb_destroy_func_t destroy);
void       hb_blob_destroy(hb_blob_t *blob);

hb_face_t *hb_face_create(hb_blob_t *blob, unsigned int index);
void       hb_face_destroy(hb_face_t *face);
void       hb_face_set_upem(hb_face_t *face, unsigned int upem);
unsigned   hb_face_get_upem(hb_face_t *face);

hb_font_t *hb_font_create(hb_face_t *face);
void       hb_font_destroy(hb_font_t *font);
void       hb_font_set_scale(hb_font_t *font, int x_scale, int y_scale);
void       hb_font_set_ppem(hb_font_t *font, unsigned int x_ppem, unsigned int y_ppem);
hb_face_t *hb_font_get_face(hb_font_t *font);

hb_buffer_t *hb_buffer_create(void);
void         hb_buffer_destroy(hb_buffer_t *buffer);
void         hb_buffer_reset(hb_buffer_t *buffer);
void         hb_buffer_add_utf8(hb_buffer_t *buffer, const char *text, int text_length, unsigned int item_offset, int item_length);
void         hb_buffer_add_utf32(hb_buffer_t *buffer, const uint32_t *text, int text_length, unsigned int item_offset, int item_length);
void         hb_buffer_set_direction(hb_buffer_t *buffer, hb_direction_t direction);
void         hb_buffer_set_script(hb_buffer_t *buffer, hb_script_t script);
void         hb_buffer_set_language(hb_buffer_t *buffer, hb_language_t language);
void         hb_buffer_guess_segment_properties(hb_buffer_t *buffer);
unsigned int hb_buffer_get_length(hb_buffer_t *buffer);
hb_glyph_info_t     *hb_buffer_get_glyph_infos(hb_buffer_t *buffer, unsigned int *length);
hb_glyph_position_t *hb_buffer_get_glyph_positions(hb_buffer_t *buffer, unsigned int *length);

void hb_shape(hb_font_t *font, hb_buffer_t *buffer, const hb_feature_t *features, unsigned int num_features);

hb_language_t hb_language_from_string(const char *str, int len);
const char   *hb_language_to_string(hb_language_t language);
hb_language_t hb_language_get_default(void);

hb_direction_t hb_direction_from_string(const char *str, int len);
hb_script_t    hb_script_from_string(const char *str, int len);
hb_direction_t hb_script_get_horizontal_direction(hb_script_t script);

typedef struct FT_FaceRec_ *FT_Face;
hb_font_t *hb_ft_font_create(FT_Face ft_face, hb_destroy_func_t destroy);
hb_face_t *hb_ft_face_create(FT_Face ft_face, hb_destroy_func_t destroy);
