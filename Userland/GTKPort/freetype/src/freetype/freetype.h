#pragma once

#include <stddef.h>
#include <stdint.h>

typedef int            FT_Error;
typedef int            FT_Int;
typedef unsigned int   FT_UInt;
typedef long           FT_Long;
typedef unsigned long  FT_ULong;
typedef short          FT_Short;
typedef unsigned short FT_UShort;
typedef signed char    FT_Char;
typedef unsigned char  FT_Byte;
typedef char           FT_String;
typedef long           FT_Fixed;
typedef long           FT_Pos;
typedef int            FT_Int32;
typedef unsigned int   FT_UInt32;
typedef void          *FT_Pointer;
typedef size_t         FT_Offset;

typedef struct FT_LibraryRec_ *FT_Library;
typedef struct FT_FaceRec_    *FT_Face;
typedef struct FT_SizeRec_    *FT_Size;
typedef struct FT_GlyphSlotRec_ *FT_GlyphSlot;

typedef struct FT_Vector_ { FT_Pos x, y; } FT_Vector;
typedef struct FT_BBox_ { FT_Pos xMin, yMin, xMax, yMax; } FT_BBox;
typedef struct FT_Matrix_ { FT_Fixed xx, xy, yx, yy; } FT_Matrix;
typedef struct FT_Generic_ { void *data; void (*finalizer)(void*); } FT_Generic;

typedef struct FT_Bitmap_ {
    unsigned int rows, width;
    int          pitch;
    unsigned char *buffer;
    unsigned short num_grays;
    unsigned char  pixel_mode;
    unsigned char  palette_mode;
    void          *palette;
} FT_Bitmap;

typedef struct FT_Glyph_Metrics_ {
    FT_Pos width, height;
    FT_Pos horiBearingX, horiBearingY, horiAdvance;
    FT_Pos vertBearingX, vertBearingY, vertAdvance;
} FT_Glyph_Metrics;

typedef struct FT_Size_Metrics_ {
    FT_UShort x_ppem, y_ppem;
    FT_Fixed  x_scale, y_scale;
    FT_Pos    ascender, descender, height, max_advance;
} FT_Size_Metrics;

typedef struct FT_SizeRec_ {
    FT_Face          face;
    FT_Generic       generic;
    FT_Size_Metrics  metrics;
} FT_SizeRec;

typedef struct FT_GlyphSlotRec_ {
    FT_Library       library;
    FT_Face          face;
    FT_GlyphSlot     next;
    FT_Glyph_Metrics metrics;
    FT_Fixed         linearHoriAdvance;
    FT_Fixed         linearVertAdvance;
    FT_Vector        advance;
    int              format;
    FT_Bitmap        bitmap;
    FT_Int           bitmap_left;
    FT_Int           bitmap_top;
} FT_GlyphSlotRec;

typedef struct FT_FaceRec_ {
    FT_Long          num_faces;
    FT_Long          face_index;
    FT_Long          face_flags;
    FT_Long          style_flags;
    FT_Long          num_glyphs;
    FT_String       *family_name;
    FT_String       *style_name;
    FT_Int           num_fixed_sizes;
    void            *available_sizes;
    FT_Int           num_charmaps;
    void            *charmaps;
    FT_Generic       generic;
    FT_BBox          bbox;
    FT_UShort        units_per_EM;
    FT_Short         ascender;
    FT_Short         descender;
    FT_Short         height;
    FT_Short         max_advance_width;
    FT_Short         max_advance_height;
    FT_Short         underline_position;
    FT_Short         underline_thickness;
    FT_GlyphSlot     glyph;
    FT_Size          size;
    void            *charmap;
} FT_FaceRec;

#define FT_FACE_FLAG_SCALABLE   (1L << 0)
#define FT_FACE_FLAG_FIXED_WIDTH (1L << 2)
#define FT_IS_SCALABLE(f)       ((f)->face_flags & FT_FACE_FLAG_SCALABLE)

#define FT_LOAD_DEFAULT      0x0
#define FT_LOAD_RENDER       (1L << 2)
#define FT_LOAD_NO_HINTING   (1L << 1)
#define FT_LOAD_NO_BITMAP    (1L << 3)

#define FT_PIXEL_MODE_NONE  0
#define FT_PIXEL_MODE_MONO  1
#define FT_PIXEL_MODE_GRAY  2

#define FT_RENDER_MODE_NORMAL 0
#define FT_RENDER_MODE_MONO   2

#define FT_ENCODING_UNICODE 0x756E6963 

FT_Error FT_Init_FreeType(FT_Library *library);
FT_Error FT_Done_FreeType(FT_Library library);
FT_Error FT_New_Face(FT_Library library, const char *path, FT_Long face_index, FT_Face *aface);
FT_Error FT_New_Memory_Face(FT_Library library, const FT_Byte *file_base, FT_Long file_size, FT_Long face_index, FT_Face *aface);
FT_Error FT_Done_Face(FT_Face face);
FT_Error FT_Set_Char_Size(FT_Face face, FT_Long char_width, FT_Long char_height, FT_UInt horz_res, FT_UInt vert_res);
FT_Error FT_Set_Pixel_Sizes(FT_Face face, FT_UInt pixel_width, FT_UInt pixel_height);
FT_UInt  FT_Get_Char_Index(FT_Face face, FT_ULong charcode);
FT_Error FT_Load_Glyph(FT_Face face, FT_UInt glyph_index, FT_Int32 load_flags);
FT_Error FT_Load_Char(FT_Face face, FT_ULong char_code, FT_Int32 load_flags);
FT_Error FT_Render_Glyph(FT_GlyphSlot slot, int render_mode);
FT_Error FT_Select_Charmap(FT_Face face, int encoding);
void     FT_Set_Transform(FT_Face face, FT_Matrix *matrix, FT_Vector *delta);
FT_Error FT_Get_Kerning(FT_Face face, FT_UInt left, FT_UInt right, FT_UInt kern_mode, FT_Vector *akerning);

void FT_Library_Version(FT_Library library, FT_Int *amajor, FT_Int *aminor, FT_Int *apatch);
