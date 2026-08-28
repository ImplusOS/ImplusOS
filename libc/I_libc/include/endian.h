#pragma once

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define __PDP_ENDIAN 3412

#define LITTLE_ENDIAN __LITTLE_ENDIAN
#define BIG_ENDIAN __BIG_ENDIAN
#define PDP_ENDIAN __PDP_ENDIAN

#if defined(__BYTE_ORDER__)
#define __BYTE_ORDER __BYTE_ORDER__
#define BYTE_ORDER __BYTE_ORDER__
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#if defined(__ORDER_LITTLE_ENDIAN__)
#define __ORDER_LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#endif

#if defined(__ORDER_BIG_ENDIAN__)
#define __ORDER_BIG_ENDIAN __ORDER_BIG_ENDIAN__
#endif

