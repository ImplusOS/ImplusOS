#include "fontconfig.h"
#include <string.h>

extern void *calloc(unsigned long, unsigned long);
extern void *malloc(unsigned long);
extern void  free(void *);
extern char *strdup(const char *);

struct _FcConfig   { int dummy; };
struct _FcPattern  { char family[64]; char style[32]; int weight, slant; double size; char file[128]; int index; };
struct _FcObjectSet { char objects[16][32]; int count; };
struct _FcCharSet  { int dummy; };
struct _FcLangSet  { int dummy; };

static struct _FcConfig g_fc_config;

FcBool FcInit(void) { return FcTrue; }
void   FcFini(void) {}
FcConfig *FcConfigGetCurrent(void) { return &g_fc_config; }

FcBool FcConfigSubstitute(FcConfig *c, FcPattern *p, FcMatchKind k) { (void)c;(void)p;(void)k; return FcTrue; }
void   FcDefaultSubstitute(FcPattern *p) { if (!p->size) p->size = 12.0; if (!p->family[0]) strcpy(p->family, "sans"); }

FcPattern *FcPatternCreate(void) { return (FcPattern*)calloc(1, sizeof(FcPattern)); }
void FcPatternDestroy(FcPattern *p) { free(p); }
FcPattern *FcPatternDuplicate(const FcPattern *p) {
    FcPattern *n = (FcPattern*)malloc(sizeof(FcPattern));
    memcpy(n, p, sizeof(FcPattern));
    return n;
}

FcBool FcPatternAddString(FcPattern *p, const char *obj, const FcChar8 *s) {
    if (strcmp(obj, FC_FAMILY) == 0 && s) { strncpy(p->family, (const char*)s, 63); p->family[63] = 0; }
    else if (strcmp(obj, FC_STYLE) == 0 && s) { strncpy(p->style, (const char*)s, 31); p->style[31] = 0; }
    else if (strcmp(obj, FC_FILE) == 0 && s) { strncpy(p->file, (const char*)s, 127); p->file[127] = 0; }
    return FcTrue;
}

FcBool FcPatternAddInteger(FcPattern *p, const char *obj, int i) {
    if (strcmp(obj, FC_WEIGHT) == 0) p->weight = i;
    else if (strcmp(obj, FC_SLANT) == 0) p->slant = i;
    else if (strcmp(obj, FC_INDEX) == 0) p->index = i;
    return FcTrue;
}

FcBool FcPatternAddDouble(FcPattern *p, const char *obj, double d) {
    if (strcmp(obj, FC_SIZE) == 0 || strcmp(obj, FC_PIXEL_SIZE) == 0) p->size = d;
    return FcTrue;
}

FcBool FcPatternAddBool(FcPattern *p, const char *obj, FcBool b) { (void)p;(void)obj;(void)b; return FcTrue; }

FcResult FcPatternGetString(const FcPattern *p, const char *obj, int n, FcChar8 **s) {
    (void)n;
    if (strcmp(obj, FC_FAMILY) == 0) { *s = (FcChar8*)p->family; return FcResultMatch; }
    if (strcmp(obj, FC_STYLE) == 0)  { *s = (FcChar8*)p->style;  return FcResultMatch; }
    if (strcmp(obj, FC_FILE) == 0)   { *s = (FcChar8*)p->file;   return FcResultMatch; }
    return FcResultNoMatch;
}

FcResult FcPatternGetInteger(const FcPattern *p, const char *obj, int n, int *i) {
    (void)n;
    if (strcmp(obj, FC_WEIGHT) == 0) { *i = p->weight; return FcResultMatch; }
    if (strcmp(obj, FC_SLANT) == 0)  { *i = p->slant;  return FcResultMatch; }
    if (strcmp(obj, FC_INDEX) == 0)  { *i = p->index;  return FcResultMatch; }
    return FcResultNoMatch;
}

FcResult FcPatternGetDouble(const FcPattern *p, const char *obj, int n, double *d) {
    (void)n;
    if (strcmp(obj, FC_SIZE) == 0 || strcmp(obj, FC_PIXEL_SIZE) == 0) { *d = p->size; return FcResultMatch; }
    return FcResultNoMatch;
}

FcResult FcPatternGetBool(const FcPattern *p, const char *obj, int n, FcBool *b) {
    (void)p;(void)obj;(void)n; *b = FcTrue; return FcResultMatch;
}

FcBool FcPatternDel(FcPattern *p, const char *obj) { (void)p;(void)obj; return FcTrue; }

FcPattern *FcFontMatch(FcConfig *c, FcPattern *p, FcResult *r) {
    (void)c;
    FcPattern *m = FcPatternDuplicate(p);
    if (!m->family[0]) strcpy(m->family, "sans");
    if (r) *r = FcResultMatch;
    return m;
}

FcFontSet *FcFontSort(FcConfig *c, FcPattern *p, FcBool trim, FcCharSet **csp, FcResult *r) {
    (void)c;(void)trim;(void)csp;
    FcFontSet *fs = (FcFontSet*)calloc(1, sizeof(FcFontSet));
    fs->fonts = (FcPattern**)malloc(sizeof(FcPattern*));
    fs->fonts[0] = FcPatternDuplicate(p);
    fs->nfont = 1; fs->sfont = 1;
    if (r) *r = FcResultMatch;
    return fs;
}

FcFontSet *FcFontList(FcConfig *c, FcPattern *p, FcObjectSet *os) {
    (void)c;(void)os;
    FcFontSet *fs = (FcFontSet*)calloc(1, sizeof(FcFontSet));
    fs->fonts = (FcPattern**)malloc(sizeof(FcPattern*));
    fs->fonts[0] = FcPatternDuplicate(p);
    fs->nfont = 1; fs->sfont = 1;
    return fs;
}

void FcFontSetDestroy(FcFontSet *fs) {
    if (!fs) return;
    for (int i = 0; i < fs->nfont; i++) FcPatternDestroy(fs->fonts[i]);
    free(fs->fonts); free(fs);
}

FcPattern *FcNameParse(const FcChar8 *name) {
    FcPattern *p = FcPatternCreate();
    if (name) FcPatternAddString(p, FC_FAMILY, name);
    return p;
}

FcChar8 *FcNameUnparse(const FcPattern *p) { return (FcChar8*)strdup(p->family[0] ? p->family : "sans"); }

FcObjectSet *FcObjectSetCreate(void) { return (FcObjectSet*)calloc(1, sizeof(FcObjectSet)); }
FcBool FcObjectSetAdd(FcObjectSet *os, const char *obj) {
    if (os->count < 16) { strncpy(os->objects[os->count], obj, 31); os->objects[os->count][31] = 0; os->count++; return FcTrue; }
    return FcFalse;
}
void FcObjectSetDestroy(FcObjectSet *os) { free(os); }

FcCharSet *FcCharSetCreate(void) { return (FcCharSet*)calloc(1, sizeof(FcCharSet)); }
void FcCharSetDestroy(FcCharSet *fcs) { free(fcs); }
FcBool FcCharSetAddChar(FcCharSet *fcs, FcChar32 u) { (void)fcs;(void)u; return FcTrue; }
FcBool FcCharSetHasChar(const FcCharSet *fcs, FcChar32 u) { (void)fcs;(void)u; return FcTrue; }
FcLangSet *FcLangSetCreate(void) { return (FcLangSet*)calloc(1, sizeof(FcLangSet)); }
void FcLangSetDestroy(FcLangSet *ls) { free(ls); }
