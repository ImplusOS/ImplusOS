#pragma once

/*
 * Keep the official mbedTLS checkout unmodified.  ImplusOS supplies entropy
 * and monotonic millisecond time through a small port shim linked by NetSurf.
 */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_PLATFORM_MS_TIME_ALT

/* The generic timing module only has Unix/Windows implementations. */
#undef MBEDTLS_TIMING_C

/* libcurl supplies the socket transport; mbedTLS net_sockets is unused here. */
#undef MBEDTLS_NET_C
