#pragma once

#include <stddef.h>
#include <stdint.h>

typedef uint32_t FriBidiChar;
typedef int      FriBidiStrIndex;
typedef uint32_t FriBidiCharType;
typedef uint32_t FriBidiParType;
typedef uint8_t  FriBidiLevel;

#define FRIBIDI_PAR_ON   0x0000
#define FRIBIDI_PAR_LTR  0x0100
#define FRIBIDI_PAR_RTL  0x0110
#define FRIBIDI_PAR_WLTR 0x0200
#define FRIBIDI_PAR_WRTL 0x0210
#define FRIBIDI_TYPE_LTR_VAL  0x0100

FriBidiParType fribidi_get_par_direction(const FriBidiCharType *bidi_types, FriBidiStrIndex len);
FriBidiLevel   fribidi_get_par_embedding_levels(FriBidiParType base_dir, const FriBidiCharType *bidi_types,
                                                 FriBidiStrIndex len, FriBidiLevel *embedding_levels);
void           fribidi_get_bidi_types(const FriBidiChar *str, FriBidiStrIndex len, FriBidiCharType *bidi_types);
FriBidiStrIndex fribidi_remove_bidi_marks(FriBidiChar *str, FriBidiStrIndex len,
                                          FriBidiStrIndex *positions_to_this, FriBidiStrIndex *position_from_this_list,
                                          FriBidiLevel *embedding_levels);
int            fribidi_log2vis(const FriBidiChar *str, FriBidiStrIndex len, FriBidiParType *pbase_dir,
                              FriBidiChar *visual_str, FriBidiStrIndex *positions_L_to_V,
                              FriBidiStrIndex *positions_V_to_L, FriBidiLevel *embedding_levels);

const char *fribidi_version_info;
