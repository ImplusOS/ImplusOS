#include <stdint.h>
#include <stdarg.h>

#include <curl/curl.h>

#include "API/Serial.h"

typedef int nserror;

static void trace_puts(const char *s)
{
    serial_write_string(s);
}

static void trace_ret(const char *name, nserror ret)
{
    trace_puts("[NetSurf:init] ");
    trace_puts(name);
    trace_puts(" -> ");
    serial_write_uint64((uint64_t)(int64_t)ret);
    trace_puts("\n");
}

static void trace_curl_ret(const char *name, long option, int ret)
{
    trace_puts("[NetSurf:curl] ");
    trace_puts(name);
    trace_puts(" option=");
    serial_write_uint64((uint64_t)option);
    trace_puts(" ret=");
    serial_write_uint64((uint64_t)(int64_t)ret);
    trace_puts("\n");
}

static const char *easy_option_name(CURLoption option)
{
    switch (option) {
    case CURLOPT_ERRORBUFFER: return "CURLOPT_ERRORBUFFER";
    case CURLOPT_DEBUGFUNCTION: return "CURLOPT_DEBUGFUNCTION";
    case CURLOPT_VERBOSE: return "CURLOPT_VERBOSE";
    case CURLOPT_HTTP_VERSION: return "CURLOPT_HTTP_VERSION";
    case CURLOPT_WRITEFUNCTION: return "CURLOPT_WRITEFUNCTION";
    case CURLOPT_HEADERFUNCTION: return "CURLOPT_HEADERFUNCTION";
    case CURLOPT_NOPROGRESS: return "CURLOPT_NOPROGRESS";
    case CURLOPT_USERAGENT: return "CURLOPT_USERAGENT";
    case CURLOPT_ACCEPT_ENCODING: return "CURLOPT_ACCEPT_ENCODING";
    case CURLOPT_LOW_SPEED_LIMIT: return "CURLOPT_LOW_SPEED_LIMIT";
    case CURLOPT_LOW_SPEED_TIME: return "CURLOPT_LOW_SPEED_TIME";
    case CURLOPT_NOSIGNAL: return "CURLOPT_NOSIGNAL";
    case CURLOPT_CONNECTTIMEOUT: return "CURLOPT_CONNECTTIMEOUT";
    case CURLOPT_OPENSOCKETFUNCTION: return "CURLOPT_OPENSOCKETFUNCTION";
    case CURLOPT_CLOSESOCKETFUNCTION: return "CURLOPT_CLOSESOCKETFUNCTION";
    case CURLOPT_CAINFO: return "CURLOPT_CAINFO";
    case CURLOPT_CAPATH: return "CURLOPT_CAPATH";
    case CURLOPT_SSL_CTX_FUNCTION: return "CURLOPT_SSL_CTX_FUNCTION";
    case CURLOPT_TLS13_CIPHERS: return "CURLOPT_TLS13_CIPHERS";
    case CURLOPT_SSL_CIPHER_LIST: return "CURLOPT_SSL_CIPHER_LIST";
    case CURLOPT_XFERINFOFUNCTION: return "CURLOPT_XFERINFOFUNCTION";
    case CURLOPT_PROGRESSFUNCTION: return "CURLOPT_PROGRESSFUNCTION";
    default: return "CURLOPT_UNKNOWN";
    }
}

static const char *multi_option_name(CURLMoption option)
{
    switch (option) {
    case CURLMOPT_MAXCONNECTS: return "CURLMOPT_MAXCONNECTS";
    case CURLMOPT_MAX_TOTAL_CONNECTIONS: return "CURLMOPT_MAX_TOTAL_CONNECTIONS";
    case CURLMOPT_MAX_HOST_CONNECTIONS: return "CURLMOPT_MAX_HOST_CONNECTIONS";
    default: return "CURLMOPT_UNKNOWN";
    }
}

static void trace_curl_named_ret(const char *name, const char *option_name,
                                 long option, int ret)
{
    trace_puts("[NetSurf:curl] ");
    trace_puts(name);
    trace_puts(" ");
    trace_puts(option_name);
    trace_puts(" (");
    serial_write_uint64((uint64_t)option);
    trace_puts(") -> ");
    serial_write_uint64((uint64_t)(int64_t)ret);
    trace_puts("\n");
}

#define WRAP0(name) \
    extern nserror __real_##name(void); \
    nserror __wrap_##name(void) \
    { \
        nserror ret; \
        trace_puts("[NetSurf:init] enter " #name "\n"); \
        ret = __real_##name(); \
        trace_ret(#name, ret); \
        return ret; \
    }

#define WRAP1(name, argtype) \
    extern nserror __real_##name(argtype); \
    nserror __wrap_##name(argtype arg) \
    { \
        nserror ret; \
        trace_puts("[NetSurf:init] enter " #name "\n"); \
        ret = __real_##name(arg); \
        trace_ret(#name, ret); \
        return ret; \
    }

WRAP0(corestrings_init)
WRAP0(nscolour_update)
WRAP1(image_cache_init, const void *)
WRAP0(nscss_init)
WRAP0(html_init)
WRAP0(image_init)
WRAP0(textplain_init)
WRAP0(fetcher_init)
WRAP1(hlcache_initialise, const void *)
WRAP1(llcache_initialise, const void *)
WRAP0(ns_system_colour_init)
WRAP0(page_info_init)

WRAP0(fetch_curl_register)
WRAP0(fetch_data_register)
WRAP0(fetch_file_register)
WRAP0(fetch_resource_register)
WRAP0(fetch_about_register)
WRAP0(fetch_javascript_register)

extern nserror __real_content_factory_register_handler(const char *mime_type,
                                                       const void *handler);
nserror __wrap_content_factory_register_handler(const char *mime_type,
                                                const void *handler)
{
    nserror ret;

    trace_puts("[NetSurf:init] enter content_factory_register_handler ");
    trace_puts(mime_type ? mime_type : "(null)");
    trace_puts("\n");
    ret = __real_content_factory_register_handler(mime_type, handler);
    trace_ret("content_factory_register_handler", ret);
    return ret;
}

extern CURLcode __real_curl_global_init(long flags);
CURLcode __wrap_curl_global_init(long flags)
{
    CURLcode ret;

    trace_puts("[NetSurf:curl] enter curl_global_init\n");
    ret = __real_curl_global_init(flags);
    trace_curl_ret("curl_global_init", flags, ret);
    return ret;
}

extern CURLM *__real_curl_multi_init(void);
CURLM *__wrap_curl_multi_init(void)
{
    CURLM *ret;

    trace_puts("[NetSurf:curl] enter curl_multi_init\n");
    ret = __real_curl_multi_init();
    trace_puts("[NetSurf:curl] curl_multi_init -> ");
    serial_write_uint64((uint64_t)(uintptr_t)ret);
    trace_puts("\n");
    return ret;
}

extern CURL *__real_curl_easy_init(void);
CURL *__wrap_curl_easy_init(void)
{
    CURL *ret;

    trace_puts("[NetSurf:curl] enter curl_easy_init\n");
    ret = __real_curl_easy_init();
    trace_puts("[NetSurf:curl] curl_easy_init -> ");
    serial_write_uint64((uint64_t)(uintptr_t)ret);
    trace_puts("\n");
    return ret;
}

extern CURLMcode __real_curl_multi_setopt(CURLM *multi_handle,
                                          CURLMoption option, ...);
CURLMcode __wrap_curl_multi_setopt(CURLM *multi_handle, CURLMoption option, ...)
{
    CURLMcode ret;
    va_list ap;

    va_start(ap, option);
    switch (option) {
    case CURLMOPT_MAXCONNECTS:
    case CURLMOPT_MAX_TOTAL_CONNECTIONS:
    case CURLMOPT_MAX_HOST_CONNECTIONS:
        ret = __real_curl_multi_setopt(multi_handle, option, va_arg(ap, long));
        break;
    default:
        ret = __real_curl_multi_setopt(multi_handle, option, va_arg(ap, void *));
        break;
    }
    va_end(ap);

    trace_curl_named_ret("curl_multi_setopt", multi_option_name(option),
                         (long)option, ret);
    return ret;
}

extern CURLcode __real_curl_easy_setopt(CURL *curl, CURLoption option, ...);
CURLcode __wrap_curl_easy_setopt(CURL *curl, CURLoption option, ...)
{
    CURLcode ret;
    va_list ap;

    va_start(ap, option);
    switch (option) {
    case CURLOPT_VERBOSE:
    case CURLOPT_HTTP_VERSION:
    case CURLOPT_NOPROGRESS:
    case CURLOPT_LOW_SPEED_LIMIT:
    case CURLOPT_LOW_SPEED_TIME:
    case CURLOPT_NOSIGNAL:
    case CURLOPT_CONNECTTIMEOUT:
        ret = __real_curl_easy_setopt(curl, option, va_arg(ap, long));
        break;
    case CURLOPT_ERRORBUFFER:
    case CURLOPT_USERAGENT:
    case CURLOPT_ACCEPT_ENCODING:
    case CURLOPT_CAINFO:
    case CURLOPT_CAPATH:
    case CURLOPT_TLS13_CIPHERS:
    case CURLOPT_SSL_CIPHER_LIST:
        ret = __real_curl_easy_setopt(curl, option, va_arg(ap, char *));
        break;
    case CURLOPT_DEBUGFUNCTION:
    case CURLOPT_WRITEFUNCTION:
    case CURLOPT_HEADERFUNCTION:
    case CURLOPT_OPENSOCKETFUNCTION:
    case CURLOPT_CLOSESOCKETFUNCTION:
    case CURLOPT_SSL_CTX_FUNCTION:
    case CURLOPT_XFERINFOFUNCTION:
    case CURLOPT_PROGRESSFUNCTION:
        ret = __real_curl_easy_setopt(curl, option, va_arg(ap, void *));
        break;
    default:
        ret = __real_curl_easy_setopt(curl, option, va_arg(ap, void *));
        break;
    }
    va_end(ap);

    trace_curl_named_ret("curl_easy_setopt", easy_option_name(option),
                         (long)option, ret);
    return ret;
}
