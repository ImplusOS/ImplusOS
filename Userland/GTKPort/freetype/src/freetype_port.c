#include "ft2build.h"
#include <string.h>

extern void *calloc(unsigned long, unsigned long);
extern void  free(void *);

struct FT_LibraryRec_ { int dummy; };

static struct FT_LibraryRec_ g_ft_lib;
static FT_FaceRec            g_ft_face;
static FT_GlyphSlotRec       g_ft_slot;
static FT_SizeRec            g_ft_size;

FT_Error FT_Init_FreeType(FT_Library *lib) {
    *lib = &g_ft_lib;
    return 0;
}

FT_Error FT_Done_FreeType(FT_Library lib) { (void)lib; return 0; }

FT_Error FT_New_Face(FT_Library lib, const char *path, FT_Long idx, FT_Face *aface) {
    (void)lib; (void)path; (void)idx;
    memset(&g_ft_face, 0, sizeof(g_ft_face));
    g_ft_face.num_glyphs = 256;
    g_ft_face.family_name = (FT_String*)"ImplusOS Sans";
    g_ft_face.style_name  = (FT_String*)"Regular";
    g_ft_face.face_flags  = FT_FACE_FLAG_SCALABLE;
    g_ft_face.units_per_EM = 1000;
    g_ft_face.ascender  = 800;
    g_ft_face.descender = -200;
    g_ft_face.height    = 1000;
    memset(&g_ft_slot, 0, sizeof(g_ft_slot));
    g_ft_slot.face = &g_ft_face;
    g_ft_face.glyph = &g_ft_slot;
    memset(&g_ft_size, 0, sizeof(g_ft_size));
    g_ft_size.face = &g_ft_face;
    g_ft_face.size = &g_ft_size;
    *aface = &g_ft_face;
    return 0;
}

FT_Error FT_New_Memory_Face(FT_Library lib, const FT_Byte *base, FT_Long size, FT_Long idx, FT_Face *aface) {
    (void)base; (void)size;
    return FT_New_Face(lib, NULL, idx, aface);
}

FT_Error FT_Done_Face(FT_Face face) { (void)face; return 0; }

FT_Error FT_Set_Char_Size(FT_Face face, FT_Long cw, FT_Long ch, FT_UInt hr, FT_UInt vr) {
    (void)cw; (void)ch; (void)hr; (void)vr;
    if (face && face->size) {
        face->size->metrics.x_ppem = 16;
        face->size->metrics.y_ppem = 16;
        face->size->metrics.ascender  = 800 * 64;
        face->size->metrics.descender = -200 * 64;
        face->size->metrics.height    = 1000 * 64;
    }
    return 0;
}

FT_Error FT_Set_Pixel_Sizes(FT_Face face, FT_UInt pw, FT_UInt ph) {
    return FT_Set_Char_Size(face, (FT_Long)pw << 6, (FT_Long)ph << 6, 72, 72);
}

FT_UInt FT_Get_Char_Index(FT_Face face, FT_ULong charcode) {
    (void)face;
    return (charcode < 256) ? (FT_UInt)charcode : 0;
}

FT_Error FT_Load_Glyph(FT_Face face, FT_UInt idx, FT_Int32 flags) {
    (void)flags;
    if (!face || !face->glyph) return 1;
    face->glyph->metrics.width = 8 * 64;
    face->glyph->metrics.height = 16 * 64;
    face->glyph->metrics.horiAdvance = 8 * 64;
    face->glyph->metrics.horiBearingX = 0;
    face->glyph->metrics.horiBearingY = 12 * 64;
    face->glyph->advance.x = 8 * 64;
    face->glyph->advance.y = 0;
    face->glyph->linearHoriAdvance = 8 * 64;
    face->glyph->bitmap.rows = 0;
    face->glyph->bitmap.width = 0;
    face->glyph->bitmap.buffer = NULL;
    (void)idx;
    return 0;
}

FT_Error FT_Load_Char(FT_Face face, FT_ULong code, FT_Int32 flags) {
    FT_UInt idx = FT_Get_Char_Index(face, code);
    return FT_Load_Glyph(face, idx, flags);
}

FT_Error FT_Render_Glyph(FT_GlyphSlot slot, int mode) {
    (void)slot; (void)mode;
    return 0;
}

FT_Error FT_Select_Charmap(FT_Face face, int encoding) {
    (void)face; (void)encoding;
    return 0;
}

void FT_Set_Transform(FT_Face face, FT_Matrix *matrix, FT_Vector *delta) {
    (void)face; (void)matrix; (void)delta;
}

FT_Error FT_Get_Kerning(FT_Face face, FT_UInt left, FT_UInt right, FT_UInt mode, FT_Vector *k) {
    (void)face; (void)left; (void)right; (void)mode;
    if (k) { k->x = 0; k->y = 0; }
    return 0;
}

void FT_Library_Version(FT_Library lib, FT_Int *maj, FT_Int *min, FT_Int *pat) {
    (void)lib;
    if (maj) *maj = 2;
    if (min) *min = 13;
    if (pat) *pat = 0;
}
