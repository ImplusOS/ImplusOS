#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

typedef char     gchar;
typedef short    gshort;
typedef int      gint;
typedef long     glong;
typedef int      gboolean;
typedef unsigned char  guchar;
typedef unsigned short gushort;
typedef unsigned int   guint;
typedef unsigned long  gulong;

typedef float    gfloat;
typedef double   gdouble;

typedef void    *gpointer;
typedef const void *gconstpointer;

typedef int8_t   gint8;
typedef uint8_t  guint8;
typedef int16_t  gint16;
typedef uint16_t guint16;
typedef int32_t  gint32;
typedef uint32_t guint32;
typedef int64_t  gint64;
typedef uint64_t guint64;

typedef size_t    gsize;
typedef long      gssize;
typedef long      goffset;
typedef uintptr_t GType;

#define TRUE  1
#define FALSE 0
#define GINT_TO_POINTER(i) ((gpointer)(glong)(i))
#define GPOINTER_TO_INT(p) ((gint)(glong)(p))
#define GUINT_TO_POINTER(u) ((gpointer)(gulong)(u))
#define GPOINTER_TO_UINT(p) ((guint)(gulong)(p))
#define G_LIKELY(x)   __builtin_expect(!!(x), 1)
#define G_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define G_GNUC_UNUSED __attribute__((unused))
#define G_GNUC_NULL_TERMINATED __attribute__((sentinel))
#define G_GNUC_PRINTF(f,a) __attribute__((format(printf,f,a)))
#define G_N_ELEMENTS(arr) (sizeof(arr)/sizeof((arr)[0]))
#define G_STRUCT_OFFSET(t,f) ((glong)offsetof(t,f))
#define G_MAXINT   0x7FFFFFFF
#define G_MININT   (-G_MAXINT-1)
#define G_MAXUINT  0xFFFFFFFFU
#define G_MAXSIZE  ((gsize)-1)
#define G_MAXINT64 ((gint64)0x7FFFFFFFFFFFFFFFLL)

#define G_TYPE_NONE       ((GType)1)
#define G_TYPE_CHAR       ((GType)3)
#define G_TYPE_BOOLEAN    ((GType)5)
#define G_TYPE_INT        ((GType)6)
#define G_TYPE_UINT       ((GType)7)
#define G_TYPE_LONG       ((GType)8)
#define G_TYPE_ULONG      ((GType)9)
#define G_TYPE_INT64      ((GType)10)
#define G_TYPE_UINT64     ((GType)11)
#define G_TYPE_FLOAT      ((GType)14)
#define G_TYPE_DOUBLE     ((GType)15)
#define G_TYPE_STRING     ((GType)16)
#define G_TYPE_POINTER    ((GType)17)
#define G_TYPE_OBJECT     ((GType)20)
#define G_TYPE_BOXED      ((GType)18)
#define G_TYPE_ENUM       ((GType)12)
#define G_TYPE_FLAGS      ((GType)13)
#define G_TYPE_INVALID    ((GType)0)

#define G_TYPE_CHECK_INSTANCE_CAST(inst,gt,ct) ((ct*)(inst))
#define G_TYPE_CHECK_CLASS_CAST(klass,gt,ct) ((ct*)(klass))
#define G_TYPE_CHECK_INSTANCE_TYPE(inst,gt) (1)
#define G_TYPE_INSTANCE_GET_CLASS(inst,gt,ct) ((ct*)g_type_class_peek(gt))

#define G_DEFINE_TYPE(TN,t_n,T_P) \
    static void t_n##_init(TN*); \
    static void t_n##_class_init(TN##Class*); \
    static gpointer t_n##_parent_class = NULL; \
    GType t_n##_get_type(void) { static GType id = 0; if (!id) id = g_type_register_static_simple(T_P, #TN, sizeof(TN##Class), (GClassInitFunc)t_n##_class_init, sizeof(TN), (GInstanceInitFunc)t_n##_init, 0); return id; }

#define G_DEFINE_TYPE_WITH_CODE(TN,t_n,T_P,_C_) \
    static void t_n##_init(TN*); \
    static void t_n##_class_init(TN##Class*); \
    static gpointer t_n##_parent_class = NULL; \
    GType t_n##_get_type(void) { static GType id = 0; if (!id) { id = g_type_register_static_simple(T_P, #TN, sizeof(TN##Class), (GClassInitFunc)t_n##_class_init, sizeof(TN), (GInstanceInitFunc)t_n##_init, 0); { _C_ } } return id; }

typedef void     (*GCallback)(void);
typedef void     (*GDestroyNotify)(gpointer data);
typedef void     (*GFunc)(gpointer data, gpointer user_data);
typedef gint     (*GCompareFunc)(gconstpointer a, gconstpointer b);
typedef gint     (*GCompareDataFunc)(gconstpointer a, gconstpointer b, gpointer data);
typedef gboolean (*GEqualFunc)(gconstpointer a, gconstpointer b);
typedef guint    (*GHashFunc)(gconstpointer key);
typedef void     (*GHFunc)(gpointer key, gpointer value, gpointer data);
typedef gboolean (*GHRFunc)(gpointer key, gpointer value, gpointer data);
typedef gpointer (*GCopyFunc)(gconstpointer src, gpointer data);
typedef void     (*GClassInitFunc)(gpointer g_class, gpointer class_data);
typedef void     (*GInstanceInitFunc)(gpointer instance, gpointer g_class);
typedef gboolean (*GSourceFunc)(gpointer user_data);

typedef struct _GList       GList;
typedef struct _GSList      GSList;
typedef struct _GHashTable  GHashTable;
typedef struct _GString     GString;
typedef struct _GError      GError;
typedef struct _GMainLoop   GMainLoop;
typedef struct _GMainContext GMainContext;
typedef struct _GSource     GSource;
typedef struct _GQuark      *GQuarkPtr;
typedef guint32              GQuark;
typedef struct _GObject     GObject;
typedef struct _GObjectClass GObjectClass;
typedef struct _GValue      GValue;
typedef struct _GParamSpec  GParamSpec;
typedef struct _GClosure    GClosure;
typedef struct _GArray      GArray;
typedef struct _GPtrArray   GPtrArray;
typedef struct _GBytes      GBytes;
typedef struct _GVariant    GVariant;
typedef struct _GVariantType GVariantType;
typedef struct _GIOChannel  GIOChannel;
typedef struct _GOptionContext GOptionContext;
typedef struct _GOptionGroup   GOptionGroup;
typedef struct _GOptionEntry   GOptionEntry;

struct _GList  { gpointer data; GList  *next, *prev; };
struct _GSList { gpointer data; GSList *next; };

GList  *g_list_append(GList *list, gpointer data);
GList  *g_list_prepend(GList *list, gpointer data);
GList  *g_list_remove(GList *list, gconstpointer data);
GList  *g_list_remove_link(GList *list, GList *llink);
GList  *g_list_delete_link(GList *list, GList *link_);
GList  *g_list_reverse(GList *list);
GList  *g_list_sort(GList *list, GCompareFunc compare_func);
GList  *g_list_first(GList *list);
GList  *g_list_last(GList *list);
GList  *g_list_nth(GList *list, guint n);
gpointer g_list_nth_data(GList *list, guint n);
GList  *g_list_find(GList *list, gconstpointer data);
guint   g_list_length(GList *list);
void    g_list_foreach(GList *list, GFunc func, gpointer user_data);
void    g_list_free(GList *list);
void    g_list_free_full(GList *list, GDestroyNotify free_func);
GList  *g_list_copy(GList *list);
GList  *g_list_insert_sorted(GList *list, gpointer data, GCompareFunc func);

GSList *g_slist_append(GSList *list, gpointer data);
GSList *g_slist_prepend(GSList *list, gpointer data);
GSList *g_slist_remove(GSList *list, gconstpointer data);
void    g_slist_free(GSList *list);
void    g_slist_free_full(GSList *list, GDestroyNotify free_func);
guint   g_slist_length(GSList *list);
void    g_slist_foreach(GSList *list, GFunc func, gpointer user_data);
GSList *g_slist_reverse(GSList *list);
GSList *g_slist_find(GSList *list, gconstpointer data);
GSList *g_slist_nth(GSList *list, guint n);
GSList *g_slist_copy(GSList *list);

GHashTable *g_hash_table_new(GHashFunc hash_func, GEqualFunc key_equal_func);
GHashTable *g_hash_table_new_full(GHashFunc hash_func, GEqualFunc key_equal_func,
                                  GDestroyNotify key_destroy, GDestroyNotify value_destroy);
gboolean    g_hash_table_insert(GHashTable *ht, gpointer key, gpointer value);
gpointer    g_hash_table_lookup(GHashTable *ht, gconstpointer key);
gboolean    g_hash_table_remove(GHashTable *ht, gconstpointer key);
gboolean    g_hash_table_contains(GHashTable *ht, gconstpointer key);
guint       g_hash_table_size(GHashTable *ht);
void        g_hash_table_foreach(GHashTable *ht, GHFunc func, gpointer user_data);
guint       g_hash_table_foreach_remove(GHashTable *ht, GHRFunc func, gpointer user_data);
void        g_hash_table_destroy(GHashTable *ht);
GList      *g_hash_table_get_keys(GHashTable *ht);
GList      *g_hash_table_get_values(GHashTable *ht);
void        g_hash_table_unref(GHashTable *ht);
GHashTable *g_hash_table_ref(GHashTable *ht);

guint  g_str_hash(gconstpointer v);
guint  g_int_hash(gconstpointer v);
guint  g_direct_hash(gconstpointer v);
gboolean g_str_equal(gconstpointer v1, gconstpointer v2);
gboolean g_int_equal(gconstpointer v1, gconstpointer v2);
gboolean g_direct_equal(gconstpointer v1, gconstpointer v2);

struct _GString { gchar *str; gsize len; gsize allocated_len; };

GString *g_string_new(const gchar *init);
GString *g_string_sized_new(gsize dfl_size);
GString *g_string_append(GString *string, const gchar *val);
GString *g_string_append_c(GString *string, gchar c);
GString *g_string_append_len(GString *string, const gchar *val, gssize len);
GString *g_string_prepend(GString *string, const gchar *val);
GString *g_string_insert(GString *string, gssize pos, const gchar *val);
GString *g_string_truncate(GString *string, gsize len);
gchar   *g_string_free(GString *string, gboolean free_segment);
void     g_string_printf(GString *string, const gchar *format, ...) G_GNUC_PRINTF(2,3);
void     g_string_append_printf(GString *string, const gchar *format, ...) G_GNUC_PRINTF(2,3);

gpointer g_malloc(gsize n_bytes);
gpointer g_malloc0(gsize n_bytes);
gpointer g_realloc(gpointer mem, gsize n_bytes);
void     g_free(gpointer mem);
gchar   *g_strdup(const gchar *str);
gchar   *g_strndup(const gchar *str, gsize n);
gchar   *g_strdup_printf(const gchar *format, ...) G_GNUC_PRINTF(1,2);
gchar   *g_strdup_vprintf(const gchar *format, va_list args);
gchar   *g_strconcat(const gchar *s1, ...) G_GNUC_NULL_TERMINATED;
gchar   *g_strjoin(const gchar *separator, ...) G_GNUC_NULL_TERMINATED;
gchar  **g_strsplit(const gchar *string, const gchar *delimiter, gint max_tokens);
void     g_strfreev(gchar **str_array);
guint    g_strv_length(gchar **str_array);
gchar   *g_stpcpy(gchar *dest, const gchar *src);
gchar   *g_strstrip(gchar *string);
gchar   *g_strchug(gchar *string);
gchar   *g_strchomp(gchar *string);
gchar   *g_ascii_strdown(const gchar *str, gssize len);
gchar   *g_ascii_strup(const gchar *str, gssize len);
gint     g_ascii_strcasecmp(const gchar *s1, const gchar *s2);
gint     g_ascii_strncasecmp(const gchar *s1, const gchar *s2, gsize n);
gboolean g_str_has_prefix(const gchar *str, const gchar *prefix);
gboolean g_str_has_suffix(const gchar *str, const gchar *suffix);
gdouble  g_ascii_strtod(const gchar *nptr, gchar **endptr);
gint64   g_ascii_strtoll(const gchar *nptr, gchar **endptr, guint base);
guint64  g_ascii_strtoull(const gchar *nptr, gchar **endptr, guint base);
gboolean g_ascii_isspace(gchar c);
gboolean g_ascii_isdigit(gchar c);
gboolean g_ascii_isalpha(gchar c);
gchar    g_ascii_tolower(gchar c);
gchar    g_ascii_toupper(gchar c);

#define g_new(t,n) ((t*)g_malloc((gsize)sizeof(t)*(gsize)(n)))
#define g_new0(t,n) ((t*)g_malloc0((gsize)sizeof(t)*(gsize)(n)))
#define g_renew(t,m,n) ((t*)g_realloc((m),(gsize)sizeof(t)*(gsize)(n)))
#define g_slice_new(t) g_new(t,1)
#define g_slice_new0(t) g_new0(t,1)
#define g_slice_free(t,p) g_free(p)
#define g_slice_alloc(s) g_malloc(s)
#define g_slice_alloc0(s) g_malloc0(s)
#define g_slice_free1(s,p) g_free(p)

gpointer g_memdup(gconstpointer mem, guint byte_size);
gpointer g_memdup2(gconstpointer mem, gsize byte_size);

struct _GError { GQuark domain; gint code; gchar *message; };

GError *g_error_new(GQuark domain, gint code, const gchar *format, ...) G_GNUC_PRINTF(3,4);
GError *g_error_new_literal(GQuark domain, gint code, const gchar *message);
void    g_error_free(GError *error);
void    g_set_error(GError **err, GQuark domain, gint code, const gchar *format, ...) G_GNUC_PRINTF(4,5);
void    g_set_error_literal(GError **err, GQuark domain, gint code, const gchar *message);
void    g_propagate_error(GError **dest, GError *src);
void    g_clear_error(GError **err);
GError *g_error_copy(const GError *error);

GQuark      g_quark_from_string(const gchar *string);
GQuark      g_quark_from_static_string(const gchar *string);
const gchar *g_quark_to_string(GQuark quark);

GMainLoop    *g_main_loop_new(GMainContext *context, gboolean is_running);
void          g_main_loop_run(GMainLoop *loop);
void          g_main_loop_quit(GMainLoop *loop);
gboolean      g_main_loop_is_running(GMainLoop *loop);
void          g_main_loop_unref(GMainLoop *loop);
GMainContext *g_main_loop_get_context(GMainLoop *loop);
GMainContext *g_main_context_default(void);
GMainContext *g_main_context_new(void);
void          g_main_context_unref(GMainContext *context);
gboolean      g_main_context_iteration(GMainContext *context, gboolean may_block);
gboolean      g_main_context_pending(GMainContext *context);
guint         g_timeout_add(guint interval, GSourceFunc function, gpointer data);
guint         g_timeout_add_full(gint priority, guint interval, GSourceFunc function, gpointer data, GDestroyNotify notify);
guint         g_idle_add(GSourceFunc function, gpointer data);
guint         g_idle_add_full(gint priority, GSourceFunc function, gpointer data, GDestroyNotify notify);
gboolean      g_source_remove(guint tag);

struct _GObject { GType g_type_instance; guint ref_count; gpointer qdata; };
struct _GObjectClass { GType g_type; void (*finalize)(GObject*); };

gpointer g_object_new(GType type, const gchar *first_prop, ...);
gpointer g_object_ref(gpointer object);
void     g_object_unref(gpointer object);
void     g_object_set(gpointer object, const gchar *first_prop, ...);
void     g_object_get(gpointer object, const gchar *first_prop, ...);
void     g_object_set_data(GObject *object, const gchar *key, gpointer data);
gpointer g_object_get_data(GObject *object, const gchar *key);
void     g_object_set_data_full(GObject *object, const gchar *key, gpointer data, GDestroyNotify destroy);
gulong   g_signal_connect_data(gpointer instance, const gchar *detailed_signal,
                               GCallback c_handler, gpointer data,
                               GDestroyNotify destroy_data, int connect_flags);
void     g_signal_emit_by_name(gpointer instance, const gchar *detailed_signal, ...);
void     g_signal_handler_disconnect(gpointer instance, gulong handler_id);
gulong   g_signal_handler_find(gpointer instance, int mask, guint signal_id,
                               GQuark detail, GClosure *closure,
                               gpointer func, gpointer data);
guint    g_signal_new(const gchar *signal_name, GType itype, int signal_flags,
                      guint class_offset, void *accumulator, gpointer accu_data,
                      void *c_marshaller, GType return_type, guint n_params, ...);

#define g_signal_connect(i,s,c,d) g_signal_connect_data((i),(s),(GCallback)(c),(d),0,0)
#define g_signal_connect_swapped(i,s,c,d) g_signal_connect_data((i),(s),(GCallback)(c),(d),0,2)

GType    g_type_register_static_simple(GType parent_type, const gchar *type_name,
                                       guint class_size, GClassInitFunc class_init,
                                       guint instance_size, GInstanceInitFunc instance_init,
                                       guint flags);
gpointer g_type_class_peek(GType type);
gpointer g_type_class_ref(GType type);
void     g_type_class_unref(gpointer g_class);
gboolean g_type_is_a(GType type, GType is_a_type);
GType    g_type_parent(GType type);
const gchar *g_type_name(GType type);
GType    g_type_from_name(const gchar *name);
void     g_type_init(void);
GType    g_type_fundamental(GType type_id);

struct _GValue { GType g_type; union { gint v_int; guint v_uint; glong v_long; gulong v_ulong;
    gint64 v_int64; guint64 v_uint64; gfloat v_float; gdouble v_double;
    gpointer v_pointer; } data[2]; };

GValue *g_value_init(GValue *value, GType g_type);
void    g_value_unset(GValue *value);
void    g_value_set_int(GValue *value, gint v_int);
gint    g_value_get_int(const GValue *value);
void    g_value_set_uint(GValue *value, guint v_uint);
guint   g_value_get_uint(const GValue *value);
void    g_value_set_boolean(GValue *value, gboolean v_boolean);
gboolean g_value_get_boolean(const GValue *value);
void    g_value_set_string(GValue *value, const gchar *v_string);
const gchar *g_value_get_string(const GValue *value);
void    g_value_set_pointer(GValue *value, gpointer v_pointer);
gpointer g_value_get_pointer(const GValue *value);
void    g_value_set_object(GValue *value, gpointer v_object);
gpointer g_value_get_object(const GValue *value);
void    g_value_set_double(GValue *value, gdouble v_double);
gdouble g_value_get_double(const GValue *value);
void    g_value_set_float(GValue *value, gfloat v_float);
gfloat  g_value_get_float(const GValue *value);

struct _GArray { gchar *data; guint len; };
struct _GPtrArray { gpointer *pdata; guint len; };

GArray   *g_array_new(gboolean zero_terminated, gboolean clear_, guint element_size);
GArray   *g_array_sized_new(gboolean zero_terminated, gboolean clear_, guint element_size, guint reserved_size);
GArray   *g_array_append_vals(GArray *array, gconstpointer data, guint len);
GArray   *g_array_set_size(GArray *array, guint length);
gchar    *g_array_free(GArray *array, gboolean free_segment);
#define   g_array_append_val(a,v) g_array_append_vals(a,&(v),1)
#define   g_array_index(a,t,i)    (((t*)(void*)(a)->data)[(i)])

GPtrArray *g_ptr_array_new(void);
GPtrArray *g_ptr_array_new_with_free_func(GDestroyNotify element_free_func);
void       g_ptr_array_add(GPtrArray *array, gpointer data);
gboolean   g_ptr_array_remove(GPtrArray *array, gpointer data);
gpointer   g_ptr_array_remove_index(GPtrArray *array, guint index_);
void       g_ptr_array_sort(GPtrArray *array, GCompareFunc compare_func);
gpointer  *g_ptr_array_free(GPtrArray *array, gboolean free_seg);
void       g_ptr_array_unref(GPtrArray *array);
void       g_ptr_array_set_free_func(GPtrArray *array, GDestroyNotify element_free_func);
#define    g_ptr_array_index(a,i) ((a)->pdata[(i)])

GBytes  *g_bytes_new(gconstpointer data, gsize size);
GBytes  *g_bytes_new_static(gconstpointer data, gsize size);
GBytes  *g_bytes_ref(GBytes *bytes);
void     g_bytes_unref(GBytes *bytes);
gconstpointer g_bytes_get_data(GBytes *bytes, gsize *size);
gsize    g_bytes_get_size(GBytes *bytes);

#define G_LOG_DOMAIN ((gchar*)0)
#define G_LOG_LEVEL_ERROR    (1<<2)
#define G_LOG_LEVEL_CRITICAL (1<<3)
#define G_LOG_LEVEL_WARNING  (1<<4)
#define G_LOG_LEVEL_MESSAGE  (1<<5)
#define G_LOG_LEVEL_INFO     (1<<6)
#define G_LOG_LEVEL_DEBUG    (1<<7)

void g_log(const gchar *domain, int level, const gchar *format, ...) G_GNUC_PRINTF(3,4);
void g_warning(const gchar *format, ...) G_GNUC_PRINTF(1,2);
void g_error(const gchar *format, ...) G_GNUC_PRINTF(1,2);
void g_critical(const gchar *format, ...) G_GNUC_PRINTF(1,2);
void g_message(const gchar *format, ...) G_GNUC_PRINTF(1,2);
void g_debug(const gchar *format, ...) G_GNUC_PRINTF(1,2);

#define g_return_if_fail(expr) do { if (!(expr)) return; } while(0)
#define g_return_val_if_fail(expr,val) do { if (!(expr)) return (val); } while(0)
#define g_assert(expr) do { if (!(expr)) g_error("assertion failed: %s", #expr); } while(0)
#define g_assert_not_reached() g_error("should not be reached")
#define g_warn_if_fail(expr) do { if (!(expr)) g_warning("check failed: %s", #expr); } while(0)

gchar   *g_utf8_next_char_ptr(const gchar *p);
guint32  g_utf8_get_char(const gchar *p);
glong    g_utf8_strlen(const gchar *p, gssize max);
gchar   *g_utf8_offset_to_pointer(const gchar *str, glong offset);
glong    g_utf8_pointer_to_offset(const gchar *str, const gchar *pos);
gboolean g_utf8_validate(const gchar *str, gssize max_len, const gchar **end);
gchar   *g_utf8_strup(const gchar *str, gssize len);
gchar   *g_utf8_strdown(const gchar *str, gssize len);
gchar   *g_utf8_casefold(const gchar *str, gssize len);
gint     g_utf8_collate(const gchar *str1, const gchar *str2);
guint32 *g_utf8_to_ucs4(const gchar *str, glong len, glong *items_read, glong *items_written, GError **error);
gchar   *g_ucs4_to_utf8(const guint32 *str, glong len, glong *items_read, glong *items_written, GError **error);

#define g_utf8_next_char(p) ((p) + g_utf8_skip[*(const guchar*)(p)])
extern const gchar g_utf8_skip[256];

const gchar *g_get_home_dir(void);
const gchar *g_get_user_data_dir(void);
const gchar *g_get_user_config_dir(void);
const gchar *g_get_user_cache_dir(void);
const gchar *g_get_tmp_dir(void);
gchar       *g_build_filename(const gchar *first, ...) G_GNUC_NULL_TERMINATED;
gchar       *g_path_get_dirname(const gchar *file_name);
gchar       *g_path_get_basename(const gchar *file_name);
gboolean     g_path_is_absolute(const gchar *file_name);
gchar       *g_get_current_dir(void);

gchar       *g_base64_encode(const guchar *data, gsize len);
guchar      *g_base64_decode(const gchar *text, gsize *out_len);

void g_usleep(gulong microseconds);
gint64 g_get_monotonic_time(void);
gint64 g_get_real_time(void);

GParamSpec *g_param_spec_int(const gchar *name, const gchar *nick, const gchar *blurb,
                             gint minimum, gint maximum, gint default_value, guint flags);
GParamSpec *g_param_spec_uint(const gchar *name, const gchar *nick, const gchar *blurb,
                              guint minimum, guint maximum, guint default_value, guint flags);
GParamSpec *g_param_spec_boolean(const gchar *name, const gchar *nick, const gchar *blurb,
                                 gboolean default_value, guint flags);
GParamSpec *g_param_spec_string(const gchar *name, const gchar *nick, const gchar *blurb,
                                const gchar *default_value, guint flags);
GParamSpec *g_param_spec_double(const gchar *name, const gchar *nick, const gchar *blurb,
                                gdouble minimum, gdouble maximum, gdouble default_value, guint flags);
GParamSpec *g_param_spec_float(const gchar *name, const gchar *nick, const gchar *blurb,
                               gfloat minimum, gfloat maximum, gfloat default_value, guint flags);
GParamSpec *g_param_spec_object(const gchar *name, const gchar *nick, const gchar *blurb,
                                GType object_type, guint flags);
GParamSpec *g_param_spec_enum(const gchar *name, const gchar *nick, const gchar *blurb,
                              GType enum_type, gint default_value, guint flags);
GParamSpec *g_param_spec_pointer(const gchar *name, const gchar *nick, const gchar *blurb, guint flags);

#define G_PARAM_READABLE   (1 << 0)
#define G_PARAM_WRITABLE   (1 << 1)
#define G_PARAM_READWRITE  (G_PARAM_READABLE | G_PARAM_WRITABLE)
#define G_PARAM_CONSTRUCT  (1 << 2)
#define G_PARAM_STATIC_STRINGS (1 << 5)

void g_object_class_install_property(GObjectClass *oclass, guint property_id, GParamSpec *pspec);
void g_object_class_install_properties(GObjectClass *oclass, guint n_pspecs, GParamSpec **pspecs);

typedef enum { G_IO_STATUS_ERROR, G_IO_STATUS_NORMAL, G_IO_STATUS_EOF, G_IO_STATUS_AGAIN } GIOStatus;
typedef enum { G_IO_IN = 1, G_IO_OUT = 4, G_IO_PRI = 2, G_IO_ERR = 8, G_IO_HUP = 16, G_IO_NVAL = 32 } GIOCondition;
typedef gboolean (*GIOFunc)(GIOChannel *source, GIOCondition condition, gpointer data);
GIOChannel *g_io_channel_unix_new(int fd);
guint       g_io_add_watch(GIOChannel *channel, GIOCondition condition, GIOFunc func, gpointer user_data);
void        g_io_channel_unref(GIOChannel *channel);

#define G_MODULE_EXPORT
