#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>

#include <curl/curl.h>
#include <libnsfb.h>
#include <libwapcaplet/libwapcaplet.h>

#include "API/Serial.h"

typedef int nserror;
struct hlcache_handle;
struct browser_window;
struct content;
typedef struct nsurl nsurl;
struct content_redraw_data;
struct rect;
struct redraw_context;
struct fbtk_widget_s;
struct llcache_handle;
struct fetch;
struct dom_hubbub_parser;
struct dom_document;
struct dom_node;
struct dom_string;
struct html_content;
struct box;
typedef int content_msg;
typedef int content_type;
typedef int dom_exception;
typedef int dom_hubbub_error;
union content_msg_data;
typedef void (*box_construct_complete_cb)(struct html_content *c,
                                          bool success);

typedef nserror (*llcache_probe_callback)(struct llcache_handle *handle,
                                          const void *event, void *pw);

typedef struct {
    int type;
    union {
        struct {
            const uint8_t *buf;
            size_t len;
        } data;
        struct {
            nserror code;
            const char *msg;
        } error;
        const char *progress_msg;
        struct {
            nsurl *from;
            nsurl *to;
        } redirect;
        const void *chain;
    } data;
} llcache_event_probe_t;

typedef struct {
    llcache_probe_callback cb;
    void *pw;
} llcache_callback_probe_t;

typedef struct {
    int type;
    union {
        const char *progress;
        struct {
            const uint8_t *buf;
            size_t len;
        } header_or_data;
        const char *error;
        const char *redirect;
        struct {
            const char *realm;
        } auth;
        const void *chain;
    } data;
} fetch_msg_probe_t;

typedef struct {
    CURL *easy;
    curl_write_callback write_cb;
    curl_write_callback header_cb;
    void *write_data;
    void *header_data;
    uint64_t body_bytes;
    uint64_t header_bytes;
    uint32_t body_chunks_logged;
    uint32_t header_chunks_logged;
} curl_probe_slot_t;

static curl_probe_slot_t curl_probe_slots[8];

struct schedule_probe_node {
    struct schedule_probe_node *next;
    struct timeval due;
    void (*callback)(void *p);
    void *p;
};

static struct schedule_probe_node *schedule_probe_list;

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
    case CURLOPT_WRITEDATA: return "CURLOPT_WRITEDATA";
    case CURLOPT_URL: return "CURLOPT_URL";
    case CURLOPT_PROXY: return "CURLOPT_PROXY";
    case CURLOPT_USERPWD: return "CURLOPT_USERPWD";
    case CURLOPT_POSTFIELDS: return "CURLOPT_POSTFIELDS";
    case CURLOPT_COOKIE: return "CURLOPT_COOKIE";
    case CURLOPT_HTTPHEADER: return "CURLOPT_HTTPHEADER";
    case CURLOPT_HEADERDATA: return "CURLOPT_HEADERDATA";
    case CURLOPT_DEBUGFUNCTION: return "CURLOPT_DEBUGFUNCTION";
    case CURLOPT_VERBOSE: return "CURLOPT_VERBOSE";
    case CURLOPT_HTTP_VERSION: return "CURLOPT_HTTP_VERSION";
    case CURLOPT_HTTPGET: return "CURLOPT_HTTPGET";
    case CURLOPT_SSL_VERIFYPEER: return "CURLOPT_SSL_VERIFYPEER";
    case CURLOPT_SSL_VERIFYHOST: return "CURLOPT_SSL_VERIFYHOST";
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
    case CURLOPT_PRIVATE: return "CURLOPT_PRIVATE";
    case CURLOPT_STDERR: return "CURLOPT_STDERR";
    case CURLOPT_SSL_CTX_FUNCTION: return "CURLOPT_SSL_CTX_FUNCTION";
    case CURLOPT_XFERINFODATA: return "CURLOPT_XFERINFODATA";
    case CURLOPT_TLS13_CIPHERS: return "CURLOPT_TLS13_CIPHERS";
    case CURLOPT_SSL_CIPHER_LIST: return "CURLOPT_SSL_CIPHER_LIST";
    case CURLOPT_SSL_SESSIONID_CACHE: return "CURLOPT_SSL_SESSIONID_CACHE";
    case CURLOPT_MIMEPOST: return "CURLOPT_MIMEPOST";
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

static curl_probe_slot_t *curl_probe_slot(CURL *easy)
{
    curl_probe_slot_t *free_slot = NULL;

    for (uint32_t i = 0; i < (uint32_t)(sizeof(curl_probe_slots) / sizeof(curl_probe_slots[0])); i++) {
        if (curl_probe_slots[i].easy == easy) {
            return &curl_probe_slots[i];
        }
        if (free_slot == NULL && curl_probe_slots[i].easy == NULL) {
            free_slot = &curl_probe_slots[i];
        }
    }

    if (free_slot != NULL) {
        free_slot->easy = easy;
        free_slot->write_cb = NULL;
        free_slot->header_cb = NULL;
        free_slot->write_data = NULL;
        free_slot->header_data = NULL;
        free_slot->body_bytes = 0;
        free_slot->header_bytes = 0;
        free_slot->body_chunks_logged = 0;
        free_slot->header_chunks_logged = 0;
    }
    return free_slot;
}

static void trace_curl_chunk(const char *name, uint64_t bytes, uint64_t total)
{
    trace_puts("[NetSurf:curl] ");
    trace_puts(name);
    trace_puts(" chunk=");
    serial_write_uint64(bytes);
    trace_puts(" total=");
    serial_write_uint64(total);
    trace_puts("\n");
}

static int timeval_cmp(const struct timeval *a, const struct timeval *b)
{
    if (a->tv_sec < b->tv_sec) {
        return -1;
    }
    if (a->tv_sec > b->tv_sec) {
        return 1;
    }
    if (a->tv_usec < b->tv_usec) {
        return -1;
    }
    if (a->tv_usec > b->tv_usec) {
        return 1;
    }
    return 0;
}

static void timeval_add_ms(struct timeval *tv, int ms)
{
    if (ms < 0) {
        ms = 0;
    }

    tv->tv_sec += (time_t)(ms / 1000);
    tv->tv_usec += (suseconds_t)((ms % 1000) * 1000);
    while (tv->tv_usec >= 1000000) {
        tv->tv_sec++;
        tv->tv_usec -= 1000000;
    }
}

static int timeval_delta_ms(const struct timeval *future,
                            const struct timeval *now)
{
    long sec = (long)(future->tv_sec - now->tv_sec);
    long usec = (long)(future->tv_usec - now->tv_usec);
    long ms;

    ms = sec * 1000 + usec / 1000;
    if (ms < 0) {
        return 0;
    }
    if (ms > 50) {
        return 50;
    }
    return (int)ms;
}

static nserror schedule_probe_remove(void (*callback)(void *p), void *p)
{
    struct schedule_probe_node *cur = schedule_probe_list;
    struct schedule_probe_node *prev = NULL;

    while (cur != NULL) {
        if (cur->callback == callback && cur->p == p) {
            struct schedule_probe_node *old = cur;

            cur = cur->next;
            if (prev == NULL) {
                schedule_probe_list = cur;
            } else {
                prev->next = cur;
            }
            free(old);
        } else {
            prev = cur;
            cur = cur->next;
        }
    }

    return 0;
}

static const char *content_msg_name(content_msg msg)
{
    switch (msg) {
    case 0: return "CONTENT_MSG_LOG";
    case 1: return "CONTENT_MSG_SSL_CERTS";
    case 2: return "CONTENT_MSG_LOADING";
    case 3: return "CONTENT_MSG_READY";
    case 4: return "CONTENT_MSG_DONE";
    case 5: return "CONTENT_MSG_ERROR";
    case 6: return "CONTENT_MSG_REDIRECT";
    case 7: return "CONTENT_MSG_STATUS";
    case 8: return "CONTENT_MSG_REFORMAT";
    case 9: return "CONTENT_MSG_REDRAW";
    case 10: return "CONTENT_MSG_REFRESH";
    case 11: return "CONTENT_MSG_DOWNLOAD";
    case 12: return "CONTENT_MSG_LINK";
    default: return "CONTENT_MSG_OTHER";
    }
}

static const char *fetch_msg_name(int msg)
{
    switch (msg) {
    case 0: return "FETCH_PROGRESS";
    case 1: return "FETCH_CERTS";
    case 2: return "FETCH_HEADER";
    case 3: return "FETCH_DATA";
    case 4: return "FETCH_FINISHED";
    case 5: return "FETCH_TIMEDOUT";
    case 6: return "FETCH_ERROR";
    case 7: return "FETCH_REDIRECT";
    case 8: return "FETCH_NOTMODIFIED";
    case 9: return "FETCH_AUTH";
    case 10: return "FETCH_CERT_ERR";
    case 11: return "FETCH_SSL_ERR";
    default: return "FETCH_OTHER";
    }
}

static const char *llcache_event_name(int event)
{
    switch (event) {
    case 0: return "LLCACHE_EVENT_GOT_CERTS";
    case 1: return "LLCACHE_EVENT_HAD_HEADERS";
    case 2: return "LLCACHE_EVENT_HAD_DATA";
    case 3: return "LLCACHE_EVENT_DONE";
    case 4: return "LLCACHE_EVENT_ERROR";
    case 5: return "LLCACHE_EVENT_PROGRESS";
    case 6: return "LLCACHE_EVENT_REDIRECT";
    default: return "LLCACHE_EVENT_OTHER";
    }
}

static nserror llcache_probe_callback_wrapper(struct llcache_handle *handle,
                                              const void *event, void *pw)
{
    llcache_callback_probe_t *probe = pw;
    const llcache_event_probe_t *ev = event;
    nserror ret;

    trace_puts("[NetSurf:llcache] event ");
    if (ev != NULL) {
        trace_puts(llcache_event_name(ev->type));
        trace_puts(" (");
        serial_write_uint64((uint64_t)(int64_t)ev->type);
        trace_puts(")");
        if (ev->type == 2) {
            trace_puts(" len=");
            serial_write_uint64((uint64_t)ev->data.data.len);
        } else if (ev->type == 4) {
            trace_puts(" err=");
            serial_write_uint64((uint64_t)(int64_t)ev->data.error.code);
            trace_puts(" msg=");
            trace_puts(ev->data.error.msg ? ev->data.error.msg : "(null)");
        }
    } else {
        trace_puts("(null)");
    }
    trace_puts("\n");

    if (probe == NULL || probe->cb == NULL) {
        return 0;
    }

    ret = probe->cb(handle, event, probe->pw);
    trace_puts("[NetSurf:llcache] client ret=");
    serial_write_uint64((uint64_t)(int64_t)ret);
    trace_puts("\n");
    return ret;
}

static size_t curl_probe_write(char *buffer, size_t size, size_t nmemb, void *userdata)
{
    size_t bytes = size * nmemb;
    curl_probe_slot_t *fallback = NULL;

    for (uint32_t i = 0; i < (uint32_t)(sizeof(curl_probe_slots) / sizeof(curl_probe_slots[0])); i++) {
        curl_probe_slot_t *slot = &curl_probe_slots[i];
        if (slot->easy != NULL && slot->write_cb != NULL) {
            if (slot->write_data == userdata) {
                slot->body_bytes += bytes;
                if (slot->body_chunks_logged < 8u) {
                    trace_curl_chunk("body", (uint64_t)bytes, slot->body_bytes);
                    slot->body_chunks_logged++;
                }
                return slot->write_cb(buffer, size, nmemb, userdata);
            }
            if (fallback == NULL) {
                fallback = slot;
            }
        }
    }
    if (fallback != NULL) {
        fallback->body_bytes += bytes;
        if (fallback->body_chunks_logged < 8u) {
            trace_curl_chunk("body", (uint64_t)bytes, fallback->body_bytes);
            fallback->body_chunks_logged++;
        }
        return fallback->write_cb(buffer, size, nmemb, userdata);
    }
    return bytes;
}

static size_t curl_probe_header(char *buffer, size_t size, size_t nmemb, void *userdata)
{
    size_t bytes = size * nmemb;
    curl_probe_slot_t *fallback = NULL;

    for (uint32_t i = 0; i < (uint32_t)(sizeof(curl_probe_slots) / sizeof(curl_probe_slots[0])); i++) {
        curl_probe_slot_t *slot = &curl_probe_slots[i];
        if (slot->easy != NULL && slot->header_cb != NULL) {
            if (slot->header_data == userdata) {
                slot->header_bytes += bytes;
                if (slot->header_chunks_logged < 8u) {
                    trace_curl_chunk("header", (uint64_t)bytes, slot->header_bytes);
                    slot->header_chunks_logged++;
                }
                return slot->header_cb(buffer, size, nmemb, userdata);
            }
            if (fallback == NULL) {
                fallback = slot;
            }
        }
    }
    if (fallback != NULL) {
        fallback->header_bytes += bytes;
        if (fallback->header_chunks_logged < 8u) {
            trace_curl_chunk("header", (uint64_t)bytes, fallback->header_bytes);
            fallback->header_chunks_logged++;
        }
        return fallback->header_cb(buffer, size, nmemb, userdata);
    }
    return bytes;
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

extern void __real_fetch_send_callback(const fetch_msg_probe_t *msg,
                                       struct fetch *fetch);
void __wrap_fetch_send_callback(const fetch_msg_probe_t *msg,
                                struct fetch *fetch)
{
    static uint32_t fetch_log_count;

    if (fetch_log_count < 64u || (msg != NULL && msg->type >= 4)) {
        trace_puts("[NetSurf:fetch] fetch_send_callback ");
        if (msg != NULL) {
            trace_puts(fetch_msg_name(msg->type));
            trace_puts(" (");
            serial_write_uint64((uint64_t)(int64_t)msg->type);
            trace_puts(")");
            if (msg->type == 2 || msg->type == 3) {
                trace_puts(" len=");
                serial_write_uint64((uint64_t)msg->data.header_or_data.len);
            } else if (msg->type == 6) {
                trace_puts(" error=");
                trace_puts(msg->data.error ? msg->data.error : "(null)");
            }
        } else {
            trace_puts("(null)");
        }
        trace_puts("\n");
        fetch_log_count++;
    }
    __real_fetch_send_callback(msg, fetch);
}

extern struct content *__real_content_factory_create_content(
    struct llcache_handle *llcache, const char *fallback_charset,
    bool quirks, lwc_string *effective_type);
struct content *__wrap_content_factory_create_content(
    struct llcache_handle *llcache, const char *fallback_charset,
    bool quirks, lwc_string *effective_type)
{
    struct content *ret;

    trace_puts("[NetSurf:content] enter content_factory_create_content\n");
    ret = __real_content_factory_create_content(llcache, fallback_charset,
                                                quirks, effective_type);
    trace_puts("[NetSurf:content] content_factory_create_content -> ");
    serial_write_uint64((uint64_t)(uintptr_t)ret);
    trace_puts("\n");
    return ret;
}

extern void __real_content_broadcast(struct content *c, content_msg msg,
                                     const union content_msg_data *data);
void __wrap_content_broadcast(struct content *c, content_msg msg,
                              const union content_msg_data *data)
{
    trace_puts("[NetSurf:content] content_broadcast ");
    trace_puts(content_msg_name(msg));
    trace_puts(" (");
    serial_write_uint64((uint64_t)(int64_t)msg);
    trace_puts(")\n");
    __real_content_broadcast(c, msg, data);
}

extern void __real_content_broadcast_error(struct content *c,
                                           nserror errorcode,
                                           const char *msg);
void __wrap_content_broadcast_error(struct content *c, nserror errorcode,
                                    const char *msg)
{
    trace_puts("[NetSurf:content] content_broadcast_error code=");
    serial_write_uint64((uint64_t)(int64_t)errorcode);
    trace_puts(" msg=");
    trace_puts(msg ? msg : "(null)");
    trace_puts("\n");
    __real_content_broadcast_error(c, errorcode, msg);
}

extern void __real_content_set_ready(struct content *c);
void __wrap_content_set_ready(struct content *c)
{
    trace_puts("[NetSurf:content] content_set_ready\n");
    __real_content_set_ready(c);
}

extern void __real_content_set_done(struct content *c);
void __wrap_content_set_done(struct content *c)
{
    trace_puts("[NetSurf:content] content_set_done\n");
    __real_content_set_done(c);
}

extern void __real_content_set_status(struct content *c,
                                      const char *status_message);
void __wrap_content_set_status(struct content *c, const char *status_message)
{
    trace_puts("[NetSurf:content] content_set_status ");
    trace_puts(status_message ? status_message : "(null)");
    trace_puts("\n");
    __real_content_set_status(c, status_message);
}

extern dom_hubbub_error __real_dom_hubbub_parser_completed(
    struct dom_hubbub_parser *parser);
dom_hubbub_error __wrap_dom_hubbub_parser_completed(
    struct dom_hubbub_parser *parser)
{
    dom_hubbub_error ret;

    trace_puts("[NetSurf:dom] enter dom_hubbub_parser_completed parser=");
    serial_write_uint64((uint64_t)(uintptr_t)parser);
    trace_puts("\n");
    ret = __real_dom_hubbub_parser_completed(parser);
    trace_puts("[NetSurf:dom] dom_hubbub_parser_completed -> ");
    serial_write_uint64((uint64_t)(int64_t)ret);
    trace_puts("\n");
    return ret;
}

extern bool __real_html_begin_conversion(struct html_content *htmlc);
bool __wrap_html_begin_conversion(struct html_content *htmlc)
{
    bool ret;

    trace_puts("[NetSurf:html] enter html_begin_conversion html=");
    serial_write_uint64((uint64_t)(uintptr_t)htmlc);
    trace_puts("\n");
    ret = __real_html_begin_conversion(htmlc);
    trace_puts("[NetSurf:html] html_begin_conversion -> ");
    serial_write_uint64(ret ? 1u : 0u);
    trace_puts("\n");
    return ret;
}

extern void __real_html_finish_conversion(struct html_content *htmlc);
void __wrap_html_finish_conversion(struct html_content *htmlc)
{
    trace_puts("[NetSurf:html] enter html_finish_conversion html=");
    serial_write_uint64((uint64_t)(uintptr_t)htmlc);
    trace_puts("\n");
    __real_html_finish_conversion(htmlc);
    trace_puts("[NetSurf:html] leave html_finish_conversion\n");
}

extern nserror __real_dom_to_box(struct dom_node *n,
                                 struct html_content *c,
                                 box_construct_complete_cb cb,
                                 void **box_conversion_context);
nserror __wrap_dom_to_box(struct dom_node *n,
                          struct html_content *c,
                          box_construct_complete_cb cb,
                          void **box_conversion_context)
{
    nserror ret;

    trace_puts("[NetSurf:html] enter dom_to_box node=");
    serial_write_uint64((uint64_t)(uintptr_t)n);
    trace_puts(" html=");
    serial_write_uint64((uint64_t)(uintptr_t)c);
    trace_puts("\n");
    ret = __real_dom_to_box(n, c, cb, box_conversion_context);
    trace_puts("[NetSurf:html] dom_to_box -> ");
    serial_write_uint64((uint64_t)(int64_t)ret);
    trace_puts(" ctx=");
    serial_write_uint64((box_conversion_context != NULL) ?
                        (uint64_t)(uintptr_t)*box_conversion_context : 0u);
    trace_puts("\n");
    return ret;
}

extern bool __real_box_normalise_block(struct box *block,
                                       const struct box *root,
                                       struct html_content *c);
bool __wrap_box_normalise_block(struct box *block, const struct box *root,
                                struct html_content *c)
{
    bool ret;

    trace_puts("[NetSurf:html] enter box_normalise_block block=");
    serial_write_uint64((uint64_t)(uintptr_t)block);
    trace_puts(" root=");
    serial_write_uint64((uint64_t)(uintptr_t)root);
    trace_puts("\n");
    ret = __real_box_normalise_block(block, root, c);
    trace_puts("[NetSurf:html] box_normalise_block -> ");
    serial_write_uint64(ret ? 1u : 0u);
    trace_puts("\n");
    return ret;
}


extern nserror __real_hlcache_handle_retrieve(
    nsurl *url, uint32_t flags, nsurl *referer, void *post,
    void *cb, void *pw, void *child, content_type accepted_types,
    struct hlcache_handle **result);
nserror __wrap_hlcache_handle_retrieve(
    nsurl *url, uint32_t flags, nsurl *referer, void *post,
    void *cb, void *pw, void *child, content_type accepted_types,
    struct hlcache_handle **result)
{
    nserror ret;

    trace_puts("[NetSurf:hlcache] retrieve flags=");
    serial_write_uint64((uint64_t)flags);
    trace_puts(" accepted=");
    serial_write_uint64((uint64_t)(int64_t)accepted_types);
    trace_puts(" cb=");
    serial_write_uint64((uint64_t)(uintptr_t)cb);
    trace_puts(" pw=");
    serial_write_uint64((uint64_t)(uintptr_t)pw);
    trace_puts("\n");

    ret = __real_hlcache_handle_retrieve(url, flags, referer, post, cb, pw,
                                         child, accepted_types, result);
    trace_puts("[NetSurf:hlcache] retrieve -> ");
    serial_write_uint64((uint64_t)(int64_t)ret);
    trace_puts(" handle=");
    serial_write_uint64((uint64_t)(uintptr_t)(result ? *result : NULL));
    trace_puts("\n");
    return ret;
}

extern nserror __real_llcache_handle_retrieve(
    nsurl *url, uint32_t flags, nsurl *referer, const void *post,
    llcache_probe_callback cb, void *pw, struct llcache_handle **result);
nserror __wrap_llcache_handle_retrieve(
    nsurl *url, uint32_t flags, nsurl *referer, const void *post,
    llcache_probe_callback cb, void *pw, struct llcache_handle **result)
{
    llcache_callback_probe_t *probe = malloc(sizeof(*probe));
    nserror ret;

    if (probe != NULL) {
        probe->cb = cb;
        probe->pw = pw;
    }

    trace_puts("[NetSurf:llcache] retrieve flags=");
    serial_write_uint64((uint64_t)flags);
    trace_puts(" cb=");
    serial_write_uint64((uint64_t)(uintptr_t)cb);
    trace_puts(" pw=");
    serial_write_uint64((uint64_t)(uintptr_t)pw);
    trace_puts("\n");

    ret = __real_llcache_handle_retrieve(
        url, flags, referer, post,
        probe != NULL ? llcache_probe_callback_wrapper : cb,
        probe != NULL ? probe : pw, result);
    trace_puts("[NetSurf:llcache] retrieve -> ");
    serial_write_uint64((uint64_t)(int64_t)ret);
    trace_puts(" handle=");
    serial_write_uint64((uint64_t)(uintptr_t)(result ? *result : NULL));
    trace_puts("\n");
    return ret;
}

extern const char *__real_llcache_handle_get_header(
    const struct llcache_handle *handle, const char *key);
const char *__wrap_llcache_handle_get_header(
    const struct llcache_handle *handle, const char *key)
{
    const char *ret = __real_llcache_handle_get_header(handle, key);

    trace_puts("[NetSurf:llcache] get_header ");
    trace_puts(key ? key : "(null)");
    trace_puts(" -> ");
    trace_puts(ret ? ret : "(null)");
    trace_puts("\n");
    return ret;
}

extern nserror __real_mimesniff_compute_effective_type(
    const char *content_type_header, const uint8_t *data, size_t len,
    bool sniff_allowed, bool image_only, lwc_string **effective_type);
nserror __wrap_mimesniff_compute_effective_type(
    const char *content_type_header, const uint8_t *data, size_t len,
    bool sniff_allowed, bool image_only, lwc_string **effective_type)
{
    nserror ret;

    trace_puts("[NetSurf:mime] sniff header=");
    trace_puts(content_type_header ? content_type_header : "(null)");
    trace_puts(" len=");
    serial_write_uint64((uint64_t)len);
    trace_puts(" sniff=");
    serial_write_uint64(sniff_allowed ? 1u : 0u);
    trace_puts(" image=");
    serial_write_uint64(image_only ? 1u : 0u);
    trace_puts("\n");

    ret = __real_mimesniff_compute_effective_type(content_type_header, data,
                                                  len, sniff_allowed,
                                                  image_only, effective_type);
    trace_puts("[NetSurf:mime] sniff -> ");
    serial_write_uint64((uint64_t)(int64_t)ret);
    trace_puts(" type=");
    serial_write_uint64((uint64_t)(uintptr_t)
                        (effective_type ? *effective_type : NULL));
    trace_puts("\n");
    return ret;
}

extern content_type __real_content_factory_type_from_mime_type(
    lwc_string *mime_type);
content_type __wrap_content_factory_type_from_mime_type(lwc_string *mime_type)
{
    content_type ret;

    trace_puts("[NetSurf:content] type_from_mime ");
    if (mime_type != NULL) {
        const char *data = lwc_string_data(mime_type);
        size_t len = lwc_string_length(mime_type);

        for (size_t i = 0; i < len; i++) {
            char ch = data[i];
            char s[2] = { ch, 0 };
            trace_puts(s);
        }
        trace_puts(" len=");
        serial_write_uint64((uint64_t)len);
    } else {
        trace_puts("(null)");
    }
    trace_puts("\n");

    ret = __real_content_factory_type_from_mime_type(mime_type);
    trace_puts("[NetSurf:content] type_from_mime -> ");
    serial_write_uint64((uint64_t)(int64_t)ret);
    trace_puts("\n");
    return ret;
}

nserror __wrap_framebuffer_schedule(int tival, void (*callback)(void *p),
                                    void *p)
{
    struct schedule_probe_node *node;
    static uint32_t schedule_log_count;
    nserror ret = schedule_probe_remove(callback, p);

    if (tival < 0 || ret != 0) {
        return ret;
    }

    node = calloc(1, sizeof(*node));
    if (node == NULL) {
        return 1;
    }

    gettimeofday(&node->due, NULL);
    timeval_add_ms(&node->due, tival);
    node->callback = callback;
    node->p = p;
    node->next = schedule_probe_list;
    schedule_probe_list = node;

    if (tival == 0 || schedule_log_count < 32u) {
        trace_puts("[NetSurf:schedule] add t=");
        serial_write_uint64((uint64_t)(int64_t)tival);
        trace_puts(" cb=");
        serial_write_uint64((uint64_t)(uintptr_t)callback);
        trace_puts(" p=");
        serial_write_uint64((uint64_t)(uintptr_t)p);
        trace_puts("\n");
        schedule_log_count++;
    }

    return 0;
}

int __wrap_schedule_run(void)
{
    struct timeval now;
    struct timeval next_due;
    bool have_next = false;
    uint32_t ran = 0;

    if (schedule_probe_list == NULL) {
        return -1;
    }

    gettimeofday(&now, NULL);

    for (;;) {
        struct schedule_probe_node *cur = schedule_probe_list;
        struct schedule_probe_node *prev = NULL;
        bool fired = false;

        while (cur != NULL) {
            if (timeval_cmp(&now, &cur->due) >= 0) {
                struct schedule_probe_node *old = cur;
                void (*callback)(void *p) = old->callback;
                void *arg = old->p;

                cur = old->next;
                if (prev == NULL) {
                    schedule_probe_list = cur;
                } else {
                    prev->next = cur;
                }

                free(old);
                callback(arg);
                ran++;
                fired = true;
                break;
            }

            prev = cur;
            cur = cur->next;
        }

        if (!fired || ran >= 32u) {
            break;
        }
        gettimeofday(&now, NULL);
    }

    if (ran != 0) {
        trace_puts("[NetSurf:schedule] ran ");
        serial_write_uint64((uint64_t)ran);
        trace_puts("\n");
    }

    for (struct schedule_probe_node *cur = schedule_probe_list;
         cur != NULL;
         cur = cur->next) {
        if (!have_next || timeval_cmp(&cur->due, &next_due) < 0) {
            next_due = cur->due;
            have_next = true;
        }
    }

    if (!have_next) {
        return -1;
    }

    return timeval_delta_ms(&next_due, &now);
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
    curl_probe_slot_t *slot;

    va_start(ap, option);
    switch (option) {
    case CURLOPT_VERBOSE:
    case CURLOPT_HTTP_VERSION:
    case CURLOPT_NOPROGRESS:
    case CURLOPT_LOW_SPEED_LIMIT:
    case CURLOPT_LOW_SPEED_TIME:
    case CURLOPT_NOSIGNAL:
    case CURLOPT_CONNECTTIMEOUT:
    case CURLOPT_HTTPGET:
    case CURLOPT_SSL_VERIFYPEER:
    case CURLOPT_SSL_VERIFYHOST:
    case CURLOPT_SSL_SESSIONID_CACHE:
        ret = __real_curl_easy_setopt(curl, option, va_arg(ap, long));
        break;
    case CURLOPT_ERRORBUFFER:
    case CURLOPT_PROXY:
    case CURLOPT_USERPWD:
    case CURLOPT_COOKIE:
    case CURLOPT_USERAGENT:
    case CURLOPT_ACCEPT_ENCODING:
    case CURLOPT_CAINFO:
    case CURLOPT_CAPATH:
    case CURLOPT_TLS13_CIPHERS:
    case CURLOPT_SSL_CIPHER_LIST:
        ret = __real_curl_easy_setopt(curl, option, va_arg(ap, char *));
        break;
    case CURLOPT_URL: {
        char *value = va_arg(ap, char *);
        trace_puts("[NetSurf:curl] set URL ");
        trace_puts(value ? value : "(null)");
        trace_puts("\n");
        ret = __real_curl_easy_setopt(curl, option, value);
        break;
    }
    case CURLOPT_WRITEDATA:
        slot = curl_probe_slot(curl);
        if (slot != NULL) {
            slot->write_data = va_arg(ap, void *);
            ret = __real_curl_easy_setopt(curl, option, slot->write_data);
        } else {
            ret = __real_curl_easy_setopt(curl, option, va_arg(ap, void *));
        }
        break;
    case CURLOPT_HEADERDATA:
        slot = curl_probe_slot(curl);
        if (slot != NULL) {
            slot->header_data = va_arg(ap, void *);
            ret = __real_curl_easy_setopt(curl, option, slot->header_data);
        } else {
            ret = __real_curl_easy_setopt(curl, option, va_arg(ap, void *));
        }
        break;
    case CURLOPT_POSTFIELDS:
    case CURLOPT_HTTPHEADER:
    case CURLOPT_PRIVATE:
    case CURLOPT_STDERR:
    case CURLOPT_XFERINFODATA:
    case CURLOPT_MIMEPOST:
        ret = __real_curl_easy_setopt(curl, option, va_arg(ap, void *));
        break;
    case CURLOPT_DEBUGFUNCTION:
    case CURLOPT_OPENSOCKETFUNCTION:
    case CURLOPT_CLOSESOCKETFUNCTION:
    case CURLOPT_SSL_CTX_FUNCTION:
    case CURLOPT_XFERINFOFUNCTION:
    case CURLOPT_PROGRESSFUNCTION:
        ret = __real_curl_easy_setopt(curl, option, va_arg(ap, void *));
        break;
    case CURLOPT_WRITEFUNCTION:
        slot = curl_probe_slot(curl);
        if (slot != NULL) {
            slot->write_cb = va_arg(ap, curl_write_callback);
            ret = __real_curl_easy_setopt(curl, option,
                                          slot->write_cb ? curl_probe_write : NULL);
        } else {
            ret = __real_curl_easy_setopt(curl, option,
                                          va_arg(ap, curl_write_callback));
        }
        break;
    case CURLOPT_HEADERFUNCTION:
        slot = curl_probe_slot(curl);
        if (slot != NULL) {
            slot->header_cb = va_arg(ap, curl_write_callback);
            ret = __real_curl_easy_setopt(curl, option,
                                          slot->header_cb ? curl_probe_header : NULL);
        } else {
            ret = __real_curl_easy_setopt(curl, option,
                                          va_arg(ap, curl_write_callback));
        }
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

extern CURLMcode __real_curl_multi_add_handle(CURLM *multi_handle,
                                              CURL *curl_handle);
CURLMcode __wrap_curl_multi_add_handle(CURLM *multi_handle, CURL *curl_handle)
{
    CURLMcode ret;

    trace_puts("[NetSurf:curl] enter curl_multi_add_handle\n");
    ret = __real_curl_multi_add_handle(multi_handle, curl_handle);
    trace_curl_ret("curl_multi_add_handle", 0, ret);
    return ret;
}

extern CURLMcode __real_curl_multi_perform(CURLM *multi_handle,
                                           int *running_handles);
CURLMcode __wrap_curl_multi_perform(CURLM *multi_handle, int *running_handles)
{
    static uint32_t perform_log_count;
    CURLMcode ret = __real_curl_multi_perform(multi_handle, running_handles);

    if (perform_log_count < 32u ||
        (running_handles != NULL && *running_handles == 0)) {
        trace_puts("[NetSurf:curl] curl_multi_perform running=");
        serial_write_uint64((uint64_t)(running_handles ? *running_handles : -1));
        trace_puts(" ret=");
        serial_write_uint64((uint64_t)(int64_t)ret);
        trace_puts("\n");
        perform_log_count++;
    }
    return ret;
}

extern CURLMsg *__real_curl_multi_info_read(CURLM *multi_handle,
                                            int *msgs_in_queue);
CURLMsg *__wrap_curl_multi_info_read(CURLM *multi_handle, int *msgs_in_queue)
{
    CURLMsg *msg = __real_curl_multi_info_read(multi_handle, msgs_in_queue);

    if (msg != NULL) {
        trace_puts("[NetSurf:curl] curl_multi_info_read msg=");
        serial_write_uint64((uint64_t)msg->msg);
        if (msg->msg == CURLMSG_DONE) {
            curl_probe_slot_t *slot = curl_probe_slot(msg->easy_handle);
            trace_puts(" result=");
            serial_write_uint64((uint64_t)(int64_t)msg->data.result);
            if (slot != NULL) {
                trace_puts(" header_bytes=");
                serial_write_uint64(slot->header_bytes);
                trace_puts(" body_bytes=");
                serial_write_uint64(slot->body_bytes);
            }
        }
        trace_puts(" queue=");
        serial_write_uint64((uint64_t)(msgs_in_queue ? *msgs_in_queue : -1));
        trace_puts("\n");
    }
    return msg;
}

extern CURLcode __real_curl_easy_getinfo(CURL *curl, CURLINFO info, ...);
CURLcode __wrap_curl_easy_getinfo(CURL *curl, CURLINFO info, ...)
{
    CURLcode ret;
    va_list ap;

    va_start(ap, info);
    switch (info) {
    case CURLINFO_RESPONSE_CODE: {
        long *value = va_arg(ap, long *);
        ret = __real_curl_easy_getinfo(curl, info, value);
        trace_puts("[NetSurf:curl] curl_easy_getinfo RESPONSE_CODE -> ");
        serial_write_uint64((uint64_t)(ret == CURLE_OK && value ? *value : -1));
        trace_puts(" ret=");
        serial_write_uint64((uint64_t)(int64_t)ret);
        trace_puts("\n");
        break;
    }
    case CURLINFO_EFFECTIVE_URL:
    case CURLINFO_CONTENT_TYPE: {
        char **value = va_arg(ap, char **);
        ret = __real_curl_easy_getinfo(curl, info, value);
        trace_puts("[NetSurf:curl] curl_easy_getinfo ");
        trace_puts(info == CURLINFO_EFFECTIVE_URL ? "EFFECTIVE_URL" : "CONTENT_TYPE");
        trace_puts(" -> ");
        trace_puts((ret == CURLE_OK && value && *value) ? *value : "(null)");
        trace_puts(" ret=");
        serial_write_uint64((uint64_t)(int64_t)ret);
        trace_puts("\n");
        break;
    }
    default:
        ret = __real_curl_easy_getinfo(curl, info, va_arg(ap, void *));
        break;
    }
    va_end(ap);
    return ret;
}

extern void __real_content_request_redraw(struct hlcache_handle *h,
                                          int x, int y,
                                          int width, int height);
void __wrap_content_request_redraw(struct hlcache_handle *h,
                                   int x, int y, int width, int height)
{
    trace_puts("[NetSurf:draw] content_request_redraw x=");
    serial_write_uint64((uint64_t)(int64_t)x);
    trace_puts(" y=");
    serial_write_uint64((uint64_t)(int64_t)y);
    trace_puts(" w=");
    serial_write_uint64((uint64_t)(int64_t)width);
    trace_puts(" h=");
    serial_write_uint64((uint64_t)(int64_t)height);
    trace_puts("\n");
    __real_content_request_redraw(h, x, y, width, height);
}

extern bool __real_browser_window_redraw(struct browser_window *bw,
                                         int x, int y,
                                         const struct rect *clip,
                                         const struct redraw_context *ctx);
bool __wrap_browser_window_redraw(struct browser_window *bw, int x, int y,
                                  const struct rect *clip,
                                  const struct redraw_context *ctx)
{
    static uint32_t redraw_log_count;
    bool ret;

    if (redraw_log_count < 32u) {
        trace_puts("[NetSurf:draw] enter browser_window_redraw x=");
        serial_write_uint64((uint64_t)(int64_t)x);
        trace_puts(" y=");
        serial_write_uint64((uint64_t)(int64_t)y);
        trace_puts("\n");
    }
    ret = __real_browser_window_redraw(bw, x, y, clip, ctx);
    if (redraw_log_count < 32u) {
        trace_puts("[NetSurf:draw] browser_window_redraw -> ");
        serial_write_uint64((uint64_t)(ret ? 1u : 0u));
        trace_puts("\n");
        redraw_log_count++;
    }
    return ret;
}

extern bool __real_content_redraw(struct hlcache_handle *h,
                                  struct content_redraw_data *data,
                                  const struct rect *clip,
                                  const struct redraw_context *ctx);
bool __wrap_content_redraw(struct hlcache_handle *h,
                           struct content_redraw_data *data,
                           const struct rect *clip,
                           const struct redraw_context *ctx)
{
    static uint32_t redraw_log_count;
    bool ret;

    if (redraw_log_count < 32u) {
        trace_puts("[NetSurf:draw] enter content_redraw\n");
    }
    ret = __real_content_redraw(h, data, clip, ctx);
    if (redraw_log_count < 32u) {
        trace_puts("[NetSurf:draw] content_redraw -> ");
        serial_write_uint64((uint64_t)(ret ? 1u : 0u));
        trace_puts("\n");
        redraw_log_count++;
    }
    return ret;
}

extern bool __real_html_redraw(struct content *c,
                               struct content_redraw_data *data,
                               const struct rect *clip,
                               const struct redraw_context *ctx);
bool __wrap_html_redraw(struct content *c,
                        struct content_redraw_data *data,
                        const struct rect *clip,
                        const struct redraw_context *ctx)
{
    static uint32_t redraw_log_count;
    bool ret;

    if (redraw_log_count < 32u) {
        trace_puts("[NetSurf:draw] enter html_redraw\n");
    }
    ret = __real_html_redraw(c, data, clip, ctx);
    if (redraw_log_count < 32u) {
        trace_puts("[NetSurf:draw] html_redraw -> ");
        serial_write_uint64((uint64_t)(ret ? 1u : 0u));
        trace_puts("\n");
        redraw_log_count++;
    }
    return ret;
}

extern int __real_fbtk_redraw(struct fbtk_widget_s *widget);
int __wrap_fbtk_redraw(struct fbtk_widget_s *widget)
{
    static uint32_t redraw_log_count;
    int ret;

    if (redraw_log_count < 16u) {
        trace_puts("[NetSurf:draw] enter fbtk_redraw\n");
    }
    ret = __real_fbtk_redraw(widget);
    if (redraw_log_count < 16u) {
        trace_puts("[NetSurf:draw] fbtk_redraw -> ");
        serial_write_uint64((uint64_t)(int64_t)ret);
        trace_puts("\n");
        redraw_log_count++;
    }
    return ret;
}

extern int __real_nsfb_update(nsfb_t *nsfb, nsfb_bbox_t *box);
int __wrap_nsfb_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    static uint32_t update_log_count;
    int ret;

    if (update_log_count < 32u) {
        if (box != NULL) {
            trace_puts("[NetSurf:draw] nsfb_update box=");
            serial_write_uint64((uint64_t)(int64_t)box->x0);
            trace_puts(",");
            serial_write_uint64((uint64_t)(int64_t)box->y0);
            trace_puts("-");
            serial_write_uint64((uint64_t)(int64_t)box->x1);
            trace_puts(",");
            serial_write_uint64((uint64_t)(int64_t)box->y1);
            trace_puts("\n");
        } else {
            trace_puts("[NetSurf:draw] nsfb_update box=(null)\n");
        }
    }
    ret = __real_nsfb_update(nsfb, box);
    if (update_log_count < 32u) {
        trace_puts("[NetSurf:draw] nsfb_update -> ");
        serial_write_uint64((uint64_t)(int64_t)ret);
        trace_puts("\n");
        update_log_count++;
    }
    return ret;
}
