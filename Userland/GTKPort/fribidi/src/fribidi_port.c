#include "fribidi.h"
#include <string.h>

const char *fribidi_version_info = "1.0.13-implus";

FriBidiParType fribidi_get_par_direction(const FriBidiCharType *bt, FriBidiStrIndex len) {
    (void)bt; (void)len;
    return FRIBIDI_PAR_LTR;
}

FriBidiLevel fribidi_get_par_embedding_levels(FriBidiParType base_dir, const FriBidiCharType *bt,
                                               FriBidiStrIndex len, FriBidiLevel *el) {
    (void)base_dir; (void)bt;
    for (FriBidiStrIndex i = 0; i < len; i++) el[i] = 0;
    return 1;
}

void fribidi_get_bidi_types(const FriBidiChar *str, FriBidiStrIndex len, FriBidiCharType *bt) {
    (void)str;
    for (FriBidiStrIndex i = 0; i < len; i++) bt[i] = FRIBIDI_TYPE_LTR_VAL;
}

FriBidiStrIndex fribidi_remove_bidi_marks(FriBidiChar *str, FriBidiStrIndex len,
                                          FriBidiStrIndex *p2t, FriBidiStrIndex *pfl,
                                          FriBidiLevel *el) {
    (void)str; (void)p2t; (void)pfl; (void)el;
    return len;
}

int fribidi_log2vis(const FriBidiChar *str, FriBidiStrIndex len, FriBidiParType *pbase_dir,
                    FriBidiChar *visual_str, FriBidiStrIndex *p_l2v,
                    FriBidiStrIndex *p_v2l, FriBidiLevel *el) {
    if (pbase_dir) *pbase_dir = FRIBIDI_PAR_LTR;
    if (visual_str && str) memcpy(visual_str, str, (size_t)len * sizeof(FriBidiChar));
    for (FriBidiStrIndex i = 0; i < len; i++) {
        if (p_l2v) p_l2v[i] = i;
        if (p_v2l) p_v2l[i] = i;
        if (el) el[i] = 0;
    }
    return len;
}
