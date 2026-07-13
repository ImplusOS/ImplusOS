#pragma once

#define PATH_MAX   260
#define NAME_MAX   255
#define PIPE_BUF   4096

#include <stdint.h>

#define ARG_MAX    131072

#ifndef INT8_MAX
#define INT8_MAX   127
#endif
#ifndef INT8_MIN
#define INT8_MIN   (-128)
#endif
#ifndef UINT8_MAX
#define UINT8_MAX  255
#endif
#ifndef INT16_MAX
#define INT16_MAX  32767
#endif
#ifndef INT16_MIN
#define INT16_MIN  (-32768)
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 65535
#endif
#ifndef INT32_MAX
#define INT32_MAX  2147483647
#endif
#ifndef INT32_MIN
#define INT32_MIN  (-2147483647 - 1)
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 4294967295U
#endif
#ifndef INT64_MAX
#define INT64_MAX  9223372036854775807LL
#endif
#ifndef INT64_MIN
#define INT64_MIN  (-9223372036854775807LL - 1LL)
#endif
#ifndef ULLONG_MAX
#define ULLONG_MAX 18446744073709551615ULL
#endif
#ifndef LLONG_MAX
#define LLONG_MAX  9223372036854775807LL
#endif
#ifndef LLONG_MIN
#define LLONG_MIN  (-9223372036854775807LL - 1LL)
#endif
#ifndef UINT64_MAX
#define UINT64_MAX 18446744073709551615ULL
#endif

#ifndef INT_MAX
#define INT_MAX    2147483647
#endif
#ifndef INT_MIN
#define INT_MIN    (-2147483647 - 1)
#endif
#ifndef UINT_MAX
#define UINT_MAX   4294967295U
#endif
#ifndef LONG_MAX
#define LONG_MAX   9223372036854775807L
#endif
#ifndef LONG_MIN
#define LONG_MIN   (-9223372036854775807L - 1L)
#endif
#ifndef ULONG_MAX
#define ULONG_MAX  18446744073709551615UL
#endif
#ifndef SHRT_MAX
#define SHRT_MAX   32767
#endif
#ifndef SHRT_MIN
#define SHRT_MIN   (-32768)
#endif
#ifndef USHRT_MAX
#define USHRT_MAX  65535
#endif
#ifndef CHAR_BIT
#define CHAR_BIT   8
#endif
#ifndef SCHAR_MAX
#define SCHAR_MAX  127
#endif
#ifndef SCHAR_MIN
#define SCHAR_MIN  (-128)
#endif
#ifndef UCHAR_MAX
#define UCHAR_MAX  255
#endif
#ifndef CHAR_MAX
#define CHAR_MAX   SCHAR_MAX
#endif
#ifndef CHAR_MIN
#define CHAR_MIN   SCHAR_MIN
#endif
#ifndef SIZE_MAX
#define SIZE_MAX   ((size_t)-1)
#endif
#ifndef SSIZE_MAX
#define SSIZE_MAX  LONG_MAX
#endif
#ifndef PTRDIFF_MAX
#define PTRDIFF_MAX LONG_MAX
#endif
#ifndef INTPTR_MAX
#define INTPTR_MAX  LONG_MAX
#endif
