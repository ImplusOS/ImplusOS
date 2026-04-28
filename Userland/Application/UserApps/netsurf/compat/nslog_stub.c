/*
 * Stub libnslog implementation for ImplusOS
 * Provides minimal no-op implementations of all nslog functions
 */
#include <nslog/nslog.h>
#include <stddef.h>
#include <stdio.h>

const char *nslog_level_name(nslog_level level) {
    switch (level) {
        case NSLOG_LEVEL_DEEPDEBUG: return "DEEPDEBUG";
        case NSLOG_LEVEL_DEBUG: return "DEBUG";
        case NSLOG_LEVEL_VERBOSE: return "VERBOSE";
        case NSLOG_LEVEL_INFO: return "INFO";
        case NSLOG_LEVEL_WARNING: return "WARNING";
        case NSLOG_LEVEL_ERROR: return "ERROR";
        case NSLOG_LEVEL_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

const char *nslog_short_level_name(nslog_level level) {
    switch (level) {
        case NSLOG_LEVEL_DEEPDEBUG: return "DD";
        case NSLOG_LEVEL_DEBUG: return "DBG";
        case NSLOG_LEVEL_VERBOSE: return "VERB";
        case NSLOG_LEVEL_INFO: return "INFO";
        case NSLOG_LEVEL_WARNING: return "WARN";
        case NSLOG_LEVEL_ERROR: return "ERR";
        case NSLOG_LEVEL_CRITICAL: return "CRIT";
        default: return "???";
    }
}

static nslog_callback log_cb = NULL;
static void *log_ctx = NULL;

void nslog__log(nslog_entry_context_t *ctx,
                const char *pattern, ...) {
    (void)ctx; (void)pattern;
    /* No-op in stub */
}

nslog_error nslog_set_render_callback(nslog_callback cb, void *context) {
    log_cb = cb;
    log_ctx = context;
    return NSLOG_NO_ERROR;
}

nslog_error nslog_uncork(void) { return NSLOG_NO_ERROR; }
void nslog_cleanup(void) {}

nslog_error nslog_filter_category_new(const char *catname, nslog_filter_t **filter) {
    (void)catname; *filter = NULL; return NSLOG_NO_ERROR;
}
nslog_error nslog_filter_level_new(nslog_level level, nslog_filter_t **filter) {
    (void)level; *filter = NULL; return NSLOG_NO_ERROR;
}
nslog_error nslog_filter_filename_new(const char *filename, nslog_filter_t **filter) {
    (void)filename; *filter = NULL; return NSLOG_NO_ERROR;
}
nslog_error nslog_filter_dirname_new(const char *dirname, nslog_filter_t **filter) {
    (void)dirname; *filter = NULL; return NSLOG_NO_ERROR;
}
nslog_error nslog_filter_funcname_new(const char *funcname, nslog_filter_t **filter) {
    (void)funcname; *filter = NULL; return NSLOG_NO_ERROR;
}
nslog_error nslog_filter_and_new(nslog_filter_t *left, nslog_filter_t *right, nslog_filter_t **filter) {
    (void)left; (void)right; *filter = NULL; return NSLOG_NO_ERROR;
}
nslog_error nslog_filter_or_new(nslog_filter_t *left, nslog_filter_t *right, nslog_filter_t **filter) {
    (void)left; (void)right; *filter = NULL; return NSLOG_NO_ERROR;
}
nslog_error nslog_filter_xor_new(nslog_filter_t *left, nslog_filter_t *right, nslog_filter_t **filter) {
    (void)left; (void)right; *filter = NULL; return NSLOG_NO_ERROR;
}
nslog_error nslog_filter_not_new(nslog_filter_t *input, nslog_filter_t **filter) {
    (void)input; *filter = NULL; return NSLOG_NO_ERROR;
}
nslog_filter_t *nslog_filter_ref(nslog_filter_t *filter) { return filter; }
nslog_filter_t *nslog_filter_unref(nslog_filter_t *filter) { (void)filter; return NULL; }
nslog_error nslog_filter_set_active(nslog_filter_t *filter, nslog_filter_t **prev) {
    (void)filter; if (prev) *prev = NULL; return NSLOG_NO_ERROR;
}
nslog_error nslog_filter_from_text(const char *input, nslog_filter_t **filter) {
    (void)input; *filter = NULL; return NSLOG_NO_ERROR;
}
