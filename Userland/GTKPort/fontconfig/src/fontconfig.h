#pragma once

#include <stddef.h>
#include <stdint.h>

typedef int      FcBool;
typedef uint32_t FcChar32;
typedef uint8_t  FcChar8;
typedef struct _FcConfig   FcConfig;
typedef struct _FcPattern  FcPattern;
typedef struct _FcFontSet  FcFontSet;
typedef struct _FcObjectSet FcObjectSet;
typedef struct _FcCharSet  FcCharSet;
typedef struct _FcLangSet  FcLangSet;

typedef enum { FcResultMatch, FcResultNoMatch, FcResultTypeMismatch, FcResultNoId, FcResultOutOfMemory } FcResult;
typedef enum { FcMatchPattern, FcMatchFont, FcMatchScan } FcMatchKind;

#define FC_FAMILY        "family"
#define FC_STYLE         "style"
#define FC_SLANT         "slant"
#define FC_WEIGHT        "weight"
#define FC_SIZE          "size"
#define FC_PIXEL_SIZE    "pixelsize"
#define FC_FILE          "file"
#define FC_INDEX         "index"
#define FC_LANG          "lang"
#define FC_CHARSET       "charset"
#define FC_SCALABLE      "scalable"
#define FC_DPI           "dpi"
#define FC_ANTIALIAS     "antialias"
#define FC_HINTING       "hinting"
#define FC_RGBA          "rgba"

#define FC_SLANT_ROMAN   0
#define FC_SLANT_ITALIC  100
#define FC_WEIGHT_REGULAR 80
#define FC_WEIGHT_BOLD   200

#define FcTrue  1
#define FcFalse 0

struct _FcFontSet { int nfont, sfont; FcPattern **fonts; };

FcBool     FcInit(void);
void       FcFini(void);
FcConfig  *FcConfigGetCurrent(void);
FcBool     FcConfigSubstitute(FcConfig *config, FcPattern *p, FcMatchKind kind);
void       FcDefaultSubstitute(FcPattern *pattern);

FcPattern *FcPatternCreate(void);
void       FcPatternDestroy(FcPattern *p);
FcPattern *FcPatternDuplicate(const FcPattern *p);
FcBool     FcPatternAddString(FcPattern *p, const char *object, const FcChar8 *s);
FcBool     FcPatternAddInteger(FcPattern *p, const char *object, int i);
FcBool     FcPatternAddDouble(FcPattern *p, const char *object, double d);
FcBool     FcPatternAddBool(FcPattern *p, const char *object, FcBool b);
FcResult   FcPatternGetString(const FcPattern *p, const char *object, int n, FcChar8 **s);
FcResult   FcPatternGetInteger(const FcPattern *p, const char *object, int n, int *i);
FcResult   FcPatternGetDouble(const FcPattern *p, const char *object, int n, double *d);
FcResult   FcPatternGetBool(const FcPattern *p, const char *object, int n, FcBool *b);
FcBool     FcPatternDel(FcPattern *p, const char *object);

FcPattern *FcFontMatch(FcConfig *config, FcPattern *p, FcResult *result);
FcFontSet *FcFontSort(FcConfig *config, FcPattern *p, FcBool trim, FcCharSet **csp, FcResult *result);
FcFontSet *FcFontList(FcConfig *config, FcPattern *p, FcObjectSet *os);
void       FcFontSetDestroy(FcFontSet *fs);

FcPattern *FcNameParse(const FcChar8 *name);
FcChar8   *FcNameUnparse(const FcPattern *p);

FcObjectSet *FcObjectSetCreate(void);
FcBool       FcObjectSetAdd(FcObjectSet *os, const char *object);
void         FcObjectSetDestroy(FcObjectSet *os);

FcCharSet *FcCharSetCreate(void);
void       FcCharSetDestroy(FcCharSet *fcs);
FcBool     FcCharSetAddChar(FcCharSet *fcs, FcChar32 ucs4);
FcBool     FcCharSetHasChar(const FcCharSet *fcs, FcChar32 ucs4);

FcLangSet *FcLangSetCreate(void);
void       FcLangSetDestroy(FcLangSet *ls);
