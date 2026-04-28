#include "glib.h"
#include <string.h>
#include <stdarg.h>

extern void *malloc(unsigned long);
extern void *calloc(unsigned long, unsigned long);
extern void *realloc(void *, unsigned long);
extern void  free(void *);
extern int   snprintf(char *, unsigned long, const char *, ...);
extern int   vsnprintf(char *, unsigned long, const char *, va_list);

const gchar g_utf8_skip[256] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,6,6,1,1,
};

gpointer g_malloc(gsize n) { void *p = malloc(n ? n : 1); return p; }
gpointer g_malloc0(gsize n) { void *p = calloc(1, n ? n : 1); return p; }
gpointer g_realloc(gpointer m, gsize n) { return realloc(m, n ? n : 1); }
void     g_free(gpointer m) { free(m); }

gpointer g_memdup(gconstpointer mem, guint n) {
    if (!mem) return NULL;
    gpointer p = g_malloc(n);
    memcpy(p, mem, n);
    return p;
}
gpointer g_memdup2(gconstpointer mem, gsize n) {
    if (!mem) return NULL;
    gpointer p = g_malloc(n);
    memcpy(p, mem, n);
    return p;
}

gchar *g_strdup(const gchar *s) {
    if (!s) return NULL;
    gsize n = strlen(s) + 1;
    gchar *d = (gchar*)g_malloc(n);
    memcpy(d, s, n);
    return d;
}

gchar *g_strndup(const gchar *s, gsize n) {
    if (!s) return NULL;
    gsize len = strlen(s);
    if (len > n) len = n;
    gchar *d = (gchar*)g_malloc(len + 1);
    memcpy(d, s, len);
    d[len] = 0;
    return d;
}

gchar *g_strdup_printf(const gchar *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return g_strdup(buf);
}

gchar *g_strdup_vprintf(const gchar *fmt, va_list ap) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    return g_strdup(buf);
}

gchar *g_strconcat(const gchar *s1, ...) {
    va_list ap;
    gsize total = 0;
    const gchar *s;
    if (!s1) return NULL;
    total = strlen(s1);
    va_start(ap, s1);
    while ((s = va_arg(ap, const gchar*)) != NULL) total += strlen(s);
    va_end(ap);
    gchar *r = (gchar*)g_malloc(total + 1);
    gchar *p = r;
    gsize n = strlen(s1); memcpy(p, s1, n); p += n;
    va_start(ap, s1);
    while ((s = va_arg(ap, const gchar*)) != NULL) { n = strlen(s); memcpy(p, s, n); p += n; }
    va_end(ap);
    *p = 0;
    return r;
}

gchar *g_strjoin(const gchar *sep, ...) {
    va_list ap;
    const gchar *s;
    gsize sep_len = sep ? strlen(sep) : 0;
    va_start(ap, sep);
    s = va_arg(ap, const gchar*);
    if (!s) { va_end(ap); return g_strdup(""); }
    GString *gs = g_string_new(s);
    while ((s = va_arg(ap, const gchar*)) != NULL) {
        if (sep) g_string_append_len(gs, sep, (gssize)sep_len);
        g_string_append(gs, s);
    }
    va_end(ap);
    return g_string_free(gs, FALSE);
}

gchar **g_strsplit(const gchar *string, const gchar *delim, gint max_tokens) {
    if (!string || !delim) return NULL;
    gsize dlen = strlen(delim);
    gint count = 0;
    const gchar *p = string;
    while (*p) {
        const gchar *f = strstr(p, delim);
        if (!f || (max_tokens > 0 && count >= max_tokens - 1)) break;
        count++; p = f + dlen;
    }
    count++;
    gchar **result = (gchar**)g_malloc((gsize)(count + 1) * sizeof(gchar*));
    p = string;
    for (gint i = 0; i < count; i++) {
        const gchar *f = (i < count - 1) ? strstr(p, delim) : NULL;
        if (max_tokens > 0 && i == max_tokens - 1) f = NULL;
        if (f) { result[i] = g_strndup(p, (gsize)(f - p)); p = f + dlen; }
        else   { result[i] = g_strdup(p); p += strlen(p); }
    }
    result[count] = NULL;
    return result;
}

void g_strfreev(gchar **v) {
    if (!v) return;
    for (gchar **p = v; *p; p++) g_free(*p);
    g_free(v);
}

guint g_strv_length(gchar **v) {
    guint n = 0;
    if (v) while (v[n]) n++;
    return n;
}

gchar *g_stpcpy(gchar *d, const gchar *s) {
    while ((*d = *s)) { d++; s++; }
    return d;
}

gchar *g_strchug(gchar *s) {
    gchar *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    return s;
}

gchar *g_strchomp(gchar *s) {
    gsize l = strlen(s);
    while (l > 0 && (s[l-1]==' '||s[l-1]=='\t'||s[l-1]=='\n'||s[l-1]=='\r')) l--;
    s[l] = 0;
    return s;
}

gchar *g_strstrip(gchar *s) { return g_strchomp(g_strchug(s)); }

gboolean g_ascii_isspace(gchar c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
gboolean g_ascii_isdigit(gchar c) { return c>='0'&&c<='9'; }
gboolean g_ascii_isalpha(gchar c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
gchar    g_ascii_tolower(gchar c) { return (c>='A'&&c<='Z') ? (gchar)(c+32) : c; }
gchar    g_ascii_toupper(gchar c) { return (c>='a'&&c<='z') ? (gchar)(c-32) : c; }

gchar *g_ascii_strdown(const gchar *s, gssize len) {
    gsize n = (len < 0) ? strlen(s) : (gsize)len;
    gchar *d = (gchar*)g_malloc(n + 1);
    for (gsize i = 0; i < n; i++) d[i] = g_ascii_tolower(s[i]);
    d[n] = 0;
    return d;
}

gchar *g_ascii_strup(const gchar *s, gssize len) {
    gsize n = (len < 0) ? strlen(s) : (gsize)len;
    gchar *d = (gchar*)g_malloc(n + 1);
    for (gsize i = 0; i < n; i++) d[i] = g_ascii_toupper(s[i]);
    d[n] = 0;
    return d;
}

gint g_ascii_strcasecmp(const gchar *s1, const gchar *s2) {
    while (*s1 && *s2) {
        gchar a = g_ascii_tolower(*s1), b = g_ascii_tolower(*s2);
        if (a != b) return a - b;
        s1++; s2++;
    }
    return (guchar)*s1 - (guchar)*s2;
}

gint g_ascii_strncasecmp(const gchar *s1, const gchar *s2, gsize n) {
    for (gsize i = 0; i < n && s1[i] && s2[i]; i++) {
        gchar a = g_ascii_tolower(s1[i]), b = g_ascii_tolower(s2[i]);
        if (a != b) return a - b;
    }
    return 0;
}

gboolean g_str_has_prefix(const gchar *s, const gchar *p) {
    return strncmp(s, p, strlen(p)) == 0;
}
gboolean g_str_has_suffix(const gchar *s, const gchar *sfx) {
    gsize sl = strlen(s), pl = strlen(sfx);
    return sl >= pl && strcmp(s + sl - pl, sfx) == 0;
}

gdouble g_ascii_strtod(const gchar *nptr, gchar **endptr) {
    (void)nptr; if (endptr) *endptr = (gchar*)nptr; return 0.0;
}
gint64 g_ascii_strtoll(const gchar *nptr, gchar **endptr, guint base) {
    (void)nptr;(void)base; if (endptr) *endptr = (gchar*)nptr; return 0;
}
guint64 g_ascii_strtoull(const gchar *nptr, gchar **endptr, guint base) {
    (void)nptr;(void)base; if (endptr) *endptr = (gchar*)nptr; return 0;
}

GString *g_string_new(const gchar *init) {
    GString *s = g_new(GString, 1);
    gsize n = init ? strlen(init) : 0;
    s->allocated_len = n + 16;
    s->str = (gchar*)g_malloc(s->allocated_len);
    if (init) memcpy(s->str, init, n);
    s->str[n] = 0;
    s->len = n;
    return s;
}

GString *g_string_sized_new(gsize dfl) {
    GString *s = g_new(GString, 1);
    s->allocated_len = dfl > 0 ? dfl : 16;
    s->str = (gchar*)g_malloc(s->allocated_len);
    s->str[0] = 0;
    s->len = 0;
    return s;
}

static void g_string_ensure(GString *s, gsize need) {
    if (s->allocated_len < need) {
        s->allocated_len = need * 2;
        s->str = (gchar*)g_realloc(s->str, s->allocated_len);
    }
}

GString *g_string_append(GString *s, const gchar *v) {
    gsize n = strlen(v);
    g_string_ensure(s, s->len + n + 1);
    memcpy(s->str + s->len, v, n);
    s->len += n;
    s->str[s->len] = 0;
    return s;
}

GString *g_string_append_c(GString *s, gchar c) {
    g_string_ensure(s, s->len + 2);
    s->str[s->len++] = c;
    s->str[s->len] = 0;
    return s;
}

GString *g_string_append_len(GString *s, const gchar *v, gssize len) {
    gsize n = (len < 0) ? strlen(v) : (gsize)len;
    g_string_ensure(s, s->len + n + 1);
    memcpy(s->str + s->len, v, n);
    s->len += n;
    s->str[s->len] = 0;
    return s;
}

GString *g_string_prepend(GString *s, const gchar *v) {
    gsize n = strlen(v);
    g_string_ensure(s, s->len + n + 1);
    memmove(s->str + n, s->str, s->len + 1);
    memcpy(s->str, v, n);
    s->len += n;
    return s;
}

GString *g_string_insert(GString *s, gssize pos, const gchar *v) {
    gsize p = (pos < 0 || (gsize)pos > s->len) ? s->len : (gsize)pos;
    gsize n = strlen(v);
    g_string_ensure(s, s->len + n + 1);
    memmove(s->str + p + n, s->str + p, s->len - p + 1);
    memcpy(s->str + p, v, n);
    s->len += n;
    return s;
}

GString *g_string_truncate(GString *s, gsize len) {
    if (len < s->len) { s->len = len; s->str[len] = 0; }
    return s;
}

gchar *g_string_free(GString *s, gboolean free_seg) {
    gchar *r = free_seg ? NULL : s->str;
    if (free_seg) g_free(s->str);
    g_free(s);
    return r;
}

void g_string_printf(GString *s, const gchar *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    s->len = 0; s->str[0] = 0;
    if (n > 0) g_string_append_len(s, buf, n);
}

void g_string_append_printf(GString *s, const gchar *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) g_string_append_len(s, buf, n);
}

GList *g_list_append(GList *list, gpointer data) {
    GList *n = g_new0(GList, 1);
    n->data = data;
    if (!list) return n;
    GList *l = list;
    while (l->next) l = l->next;
    l->next = n; n->prev = l;
    return list;
}

GList *g_list_prepend(GList *list, gpointer data) {
    GList *n = g_new0(GList, 1);
    n->data = data;
    n->next = list;
    if (list) list->prev = n;
    return n;
}

GList *g_list_remove(GList *list, gconstpointer data) {
    for (GList *l = list; l; l = l->next) {
        if (l->data == data) {
            if (l->prev) l->prev->next = l->next;
            if (l->next) l->next->prev = l->prev;
            GList *r = (l == list) ? l->next : list;
            g_free(l);
            return r;
        }
    }
    return list;
}

GList *g_list_remove_link(GList *list, GList *llink) {
    if (!llink) return list;
    if (llink->prev) llink->prev->next = llink->next;
    if (llink->next) llink->next->prev = llink->prev;
    GList *r = (llink == list) ? llink->next : list;
    llink->prev = llink->next = NULL;
    return r;
}

GList *g_list_delete_link(GList *list, GList *link_) {
    list = g_list_remove_link(list, link_);
    g_free(link_);
    return list;
}

GList *g_list_reverse(GList *list) {
    GList *r = NULL;
    while (list) { GList *n = list->next; list->next = r; list->prev = NULL; if (r) r->prev = list; r = list; list = n; }
    return r;
}

GList *g_list_first(GList *list) { while (list && list->prev) list = list->prev; return list; }
GList *g_list_last(GList *list)  { while (list && list->next) list = list->next; return list; }
GList *g_list_nth(GList *list, guint n) { while (list && n--) list = list->next; return list; }
gpointer g_list_nth_data(GList *list, guint n) { GList *l = g_list_nth(list, n); return l ? l->data : NULL; }
GList *g_list_find(GList *list, gconstpointer data) { for (GList *l = list; l; l = l->next) if (l->data == data) return l; return NULL; }
guint  g_list_length(GList *list) { guint n = 0; for (GList *l = list; l; l = l->next) n++; return n; }
void   g_list_foreach(GList *list, GFunc func, gpointer ud) { for (GList *l = list; l; l = l->next) func(l->data, ud); }
void   g_list_free(GList *list) { while (list) { GList *n = list->next; g_free(list); list = n; } }
void   g_list_free_full(GList *list, GDestroyNotify f) { while (list) { GList *n = list->next; f(list->data); g_free(list); list = n; } }
GList *g_list_copy(GList *list) { GList *r = NULL; for (GList *l = list; l; l = l->next) r = g_list_append(r, l->data); return r; }

GList *g_list_sort(GList *list, GCompareFunc cmp) {
    
    if (!list || !list->next) return list;
    GList *sorted = NULL;
    while (list) {
        GList *cur = list; list = list->next; cur->prev = cur->next = NULL;
        if (!sorted || cmp(cur->data, sorted->data) <= 0) { cur->next = sorted; if (sorted) sorted->prev = cur; sorted = cur; }
        else { GList *p = sorted; while (p->next && cmp(cur->data, p->next->data) > 0) p = p->next;
               cur->next = p->next; cur->prev = p; if (p->next) p->next->prev = cur; p->next = cur; }
    }
    return sorted;
}

GList *g_list_insert_sorted(GList *list, gpointer data, GCompareFunc func) {
    GList *n = g_new0(GList, 1); n->data = data;
    if (!list || func(data, list->data) <= 0) { n->next = list; if (list) list->prev = n; return n; }
    GList *p = list;
    while (p->next && func(data, p->next->data) > 0) p = p->next;
    n->next = p->next; n->prev = p; if (p->next) p->next->prev = n; p->next = n;
    return list;
}

GSList *g_slist_append(GSList *list, gpointer data) {
    GSList *n = g_new0(GSList, 1); n->data = data;
    if (!list) return n;
    GSList *l = list; while (l->next) l = l->next; l->next = n;
    return list;
}
GSList *g_slist_prepend(GSList *list, gpointer data) { GSList *n = g_new0(GSList, 1); n->data = data; n->next = list; return n; }
GSList *g_slist_remove(GSList *list, gconstpointer data) {
    GSList **pp = &list;
    while (*pp) { if ((*pp)->data == data) { GSList *rm = *pp; *pp = rm->next; g_free(rm); return list; } pp = &(*pp)->next; }
    return list;
}
void g_slist_free(GSList *list) { while (list) { GSList *n = list->next; g_free(list); list = n; } }
void g_slist_free_full(GSList *list, GDestroyNotify f) { while (list) { GSList *n = list->next; f(list->data); g_free(list); list = n; } }
guint g_slist_length(GSList *list) { guint n = 0; for (GSList *l = list; l; l = l->next) n++; return n; }
void g_slist_foreach(GSList *list, GFunc func, gpointer ud) { for (GSList *l = list; l; l = l->next) func(l->data, ud); }
GSList *g_slist_reverse(GSList *list) { GSList *r = NULL; while (list) { GSList *n = list->next; list->next = r; r = list; list = n; } return r; }
GSList *g_slist_find(GSList *list, gconstpointer data) { for (GSList *l = list; l; l = l->next) if (l->data == data) return l; return NULL; }
GSList *g_slist_nth(GSList *list, guint n) { while (list && n--) list = list->next; return list; }
GSList *g_slist_copy(GSList *list) { GSList *r = NULL; for (GSList *l = list; l; l = l->next) r = g_slist_append(r, l->data); return r; }

#define HT_SIZE 256
typedef struct _HTEntry { gpointer key, value; struct _HTEntry *next; } HTEntry;
struct _GHashTable {
    HTEntry *buckets[HT_SIZE];
    GHashFunc hash_func; GEqualFunc equal_func;
    GDestroyNotify key_destroy, value_destroy;
    guint size; guint ref_count;
};

GHashTable *g_hash_table_new(GHashFunc hf, GEqualFunc ef) { return g_hash_table_new_full(hf, ef, NULL, NULL); }
GHashTable *g_hash_table_new_full(GHashFunc hf, GEqualFunc ef, GDestroyNotify kd, GDestroyNotify vd) {
    GHashTable *ht = g_new0(GHashTable, 1);
    ht->hash_func = hf ? hf : g_direct_hash; ht->equal_func = ef ? ef : g_direct_equal;
    ht->key_destroy = kd; ht->value_destroy = vd; ht->ref_count = 1;
    return ht;
}

gboolean g_hash_table_insert(GHashTable *ht, gpointer key, gpointer value) {
    guint idx = ht->hash_func(key) % HT_SIZE;
    for (HTEntry *e = ht->buckets[idx]; e; e = e->next) {
        if (ht->equal_func(e->key, key)) {
            if (ht->value_destroy) ht->value_destroy(e->value);
            if (ht->key_destroy) ht->key_destroy(e->key);
            e->key = key; e->value = value; return TRUE;
        }
    }
    HTEntry *n = g_new(HTEntry, 1); n->key = key; n->value = value; n->next = ht->buckets[idx];
    ht->buckets[idx] = n; ht->size++;
    return FALSE;
}

gpointer g_hash_table_lookup(GHashTable *ht, gconstpointer key) {
    guint idx = ht->hash_func(key) % HT_SIZE;
    for (HTEntry *e = ht->buckets[idx]; e; e = e->next)
        if (ht->equal_func(e->key, key)) return e->value;
    return NULL;
}

gboolean g_hash_table_remove(GHashTable *ht, gconstpointer key) {
    guint idx = ht->hash_func(key) % HT_SIZE;
    HTEntry **pp = &ht->buckets[idx];
    while (*pp) {
        if (ht->equal_func((*pp)->key, key)) {
            HTEntry *rm = *pp; *pp = rm->next;
            if (ht->key_destroy) ht->key_destroy(rm->key);
            if (ht->value_destroy) ht->value_destroy(rm->value);
            g_free(rm); ht->size--; return TRUE;
        }
        pp = &(*pp)->next;
    }
    return FALSE;
}

gboolean g_hash_table_contains(GHashTable *ht, gconstpointer key) {
    guint idx = ht->hash_func(key) % HT_SIZE;
    for (HTEntry *e = ht->buckets[idx]; e; e = e->next)
        if (ht->equal_func(e->key, key)) return TRUE;
    return FALSE;
}

guint g_hash_table_size(GHashTable *ht) { return ht->size; }

void g_hash_table_foreach(GHashTable *ht, GHFunc func, gpointer ud) {
    for (guint i = 0; i < HT_SIZE; i++)
        for (HTEntry *e = ht->buckets[i]; e; e = e->next) func(e->key, e->value, ud);
}

guint g_hash_table_foreach_remove(GHashTable *ht, GHRFunc func, gpointer ud) {
    guint removed = 0;
    for (guint i = 0; i < HT_SIZE; i++) {
        HTEntry **pp = &ht->buckets[i];
        while (*pp) {
            if (func((*pp)->key, (*pp)->value, ud)) {
                HTEntry *rm = *pp; *pp = rm->next;
                if (ht->key_destroy) ht->key_destroy(rm->key);
                if (ht->value_destroy) ht->value_destroy(rm->value);
                g_free(rm); ht->size--; removed++;
            } else pp = &(*pp)->next;
        }
    }
    return removed;
}

static void ht_destroy_entries(GHashTable *ht) {
    for (guint i = 0; i < HT_SIZE; i++) {
        HTEntry *e = ht->buckets[i];
        while (e) {
            HTEntry *n = e->next;
            if (ht->key_destroy) ht->key_destroy(e->key);
            if (ht->value_destroy) ht->value_destroy(e->value);
            g_free(e); e = n;
        }
        ht->buckets[i] = NULL;
    }
    ht->size = 0;
}

void g_hash_table_destroy(GHashTable *ht) { if (!ht) return; ht_destroy_entries(ht); g_free(ht); }
void g_hash_table_unref(GHashTable *ht) { if (!ht) return; if (--ht->ref_count == 0) g_hash_table_destroy(ht); }
GHashTable *g_hash_table_ref(GHashTable *ht) { if (ht) ht->ref_count++; return ht; }

GList *g_hash_table_get_keys(GHashTable *ht) {
    GList *r = NULL;
    for (guint i = 0; i < HT_SIZE; i++) for (HTEntry *e = ht->buckets[i]; e; e = e->next) r = g_list_append(r, e->key);
    return r;
}
GList *g_hash_table_get_values(GHashTable *ht) {
    GList *r = NULL;
    for (guint i = 0; i < HT_SIZE; i++) for (HTEntry *e = ht->buckets[i]; e; e = e->next) r = g_list_append(r, e->value);
    return r;
}

guint g_str_hash(gconstpointer v) {
    const char *s = (const char*)v;
    guint h = 5381;
    while (*s) h = h * 33 + (guchar)*s++;
    return h;
}
guint g_int_hash(gconstpointer v) { return *(const guint*)v; }
guint g_direct_hash(gconstpointer v) { return (guint)(uintptr_t)v; }
gboolean g_str_equal(gconstpointer a, gconstpointer b) { return strcmp((const char*)a, (const char*)b) == 0; }
gboolean g_int_equal(gconstpointer a, gconstpointer b) { return *(const gint*)a == *(const gint*)b; }
gboolean g_direct_equal(gconstpointer a, gconstpointer b) { return a == b; }

GError *g_error_new_literal(GQuark domain, gint code, const gchar *message) {
    GError *e = g_new(GError, 1); e->domain = domain; e->code = code; e->message = g_strdup(message); return e;
}
GError *g_error_new(GQuark domain, gint code, const gchar *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    return g_error_new_literal(domain, code, buf);
}
void g_error_free(GError *e) { if (e) { g_free(e->message); g_free(e); } }
GError *g_error_copy(const GError *e) { return e ? g_error_new_literal(e->domain, e->code, e->message) : NULL; }
void g_set_error(GError **err, GQuark d, gint c, const gchar *fmt, ...) {
    if (!err || *err) return;
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    *err = g_error_new_literal(d, c, buf);
}
void g_set_error_literal(GError **err, GQuark d, gint c, const gchar *m) { if (err && !*err) *err = g_error_new_literal(d, c, m); }
void g_propagate_error(GError **dest, GError *src) { if (dest && !*dest) *dest = src; else g_error_free(src); }
void g_clear_error(GError **err) { if (err && *err) { g_error_free(*err); *err = NULL; } }

static const gchar *quark_strings[256];
static guint quark_count = 1;

GQuark g_quark_from_string(const gchar *s) {
    if (!s) return 0;
    for (guint i = 1; i < quark_count; i++) if (strcmp(quark_strings[i], s) == 0) return i;
    if (quark_count < 256) { quark_strings[quark_count] = g_strdup(s); return quark_count++; }
    return 0;
}
GQuark g_quark_from_static_string(const gchar *s) {
    if (!s) return 0;
    for (guint i = 1; i < quark_count; i++) if (strcmp(quark_strings[i], s) == 0) return i;
    if (quark_count < 256) { quark_strings[quark_count] = s; return quark_count++; }
    return 0;
}
const gchar *g_quark_to_string(GQuark q) { return q < quark_count ? quark_strings[q] : NULL; }

void g_log(const gchar *domain, int level, const gchar *fmt, ...) { (void)domain;(void)level;(void)fmt; }
void g_warning(const gchar *fmt, ...)  { (void)fmt; }
void g_error(const gchar *fmt, ...)    { (void)fmt; for(;;); }
void g_critical(const gchar *fmt, ...) { (void)fmt; }
void g_message(const gchar *fmt, ...)  { (void)fmt; }
void g_debug(const gchar *fmt, ...)    { (void)fmt; }

struct _GMainLoop { GMainContext *ctx; gboolean running; guint ref_count; };
struct _GMainContext { guint ref_count; };

static GMainContext default_context = { .ref_count = 1 };

GMainContext *g_main_context_default(void) { return &default_context; }
GMainContext *g_main_context_new(void) { GMainContext *c = g_new0(GMainContext, 1); c->ref_count = 1; return c; }
void g_main_context_unref(GMainContext *c) { if (c && c != &default_context && --c->ref_count == 0) g_free(c); }
gboolean g_main_context_iteration(GMainContext *c, gboolean may_block) { (void)c;(void)may_block; return FALSE; }
gboolean g_main_context_pending(GMainContext *c) { (void)c; return FALSE; }

GMainLoop *g_main_loop_new(GMainContext *ctx, gboolean running) {
    GMainLoop *l = g_new0(GMainLoop, 1);
    l->ctx = ctx ? ctx : g_main_context_default();
    l->running = running; l->ref_count = 1;
    return l;
}
void g_main_loop_run(GMainLoop *l) { l->running = TRUE; while (l->running) g_main_context_iteration(l->ctx, TRUE); }
void g_main_loop_quit(GMainLoop *l) { l->running = FALSE; }
gboolean g_main_loop_is_running(GMainLoop *l) { return l->running; }
void g_main_loop_unref(GMainLoop *l) { if (l && --l->ref_count == 0) g_free(l); }
GMainContext *g_main_loop_get_context(GMainLoop *l) { return l->ctx; }

guint g_timeout_add(guint interval, GSourceFunc func, gpointer data) { (void)interval;(void)func;(void)data; return 1; }
guint g_timeout_add_full(gint p, guint i, GSourceFunc f, gpointer d, GDestroyNotify n) { (void)p;(void)i;(void)f;(void)d;(void)n; return 1; }
guint g_idle_add(GSourceFunc func, gpointer data) { (void)func;(void)data; return 1; }
guint g_idle_add_full(gint p, GSourceFunc f, gpointer d, GDestroyNotify n) { (void)p;(void)f;(void)d;(void)n; return 1; }
gboolean g_source_remove(guint tag) { (void)tag; return TRUE; }

#define MAX_TYPES 512
static struct { GType parent; const gchar *name; guint class_size, instance_size;
    GClassInitFunc class_init; GInstanceInitFunc instance_init; gpointer class_data; } type_registry[MAX_TYPES];
static guint type_count = 64;

void g_type_init(void) {}

GType g_type_register_static_simple(GType parent, const gchar *name, guint class_size,
    GClassInitFunc ci, guint inst_size, GInstanceInitFunc ii, guint flags) {
    (void)flags;
    if (type_count >= MAX_TYPES) return 0;
    guint id = type_count++;
    type_registry[id].parent = parent; type_registry[id].name = name;
    type_registry[id].class_size = class_size; type_registry[id].instance_size = inst_size;
    type_registry[id].class_init = ci; type_registry[id].instance_init = ii;
    return (GType)id;
}

gpointer g_type_class_peek(GType type) { (void)type; return NULL; }
gpointer g_type_class_ref(GType type) { (void)type; return NULL; }
void g_type_class_unref(gpointer g_class) { (void)g_class; }
gboolean g_type_is_a(GType type, GType is_a) { (void)type; (void)is_a; return TRUE; }
GType g_type_parent(GType type) { return type < type_count ? type_registry[type].parent : 0; }
const gchar *g_type_name(GType type) { return type < type_count ? type_registry[type].name : "unknown"; }
GType g_type_from_name(const gchar *name) { for (guint i = 0; i < type_count; i++) if (type_registry[i].name && strcmp(type_registry[i].name, name) == 0) return (GType)i; return 0; }
GType g_type_fundamental(GType id) { (void)id; return G_TYPE_OBJECT; }

gpointer g_object_new(GType type, const gchar *first_prop, ...) {
    (void)first_prop;
    guint inst_size = (type < type_count) ? type_registry[type].instance_size : sizeof(GObject);
    if (inst_size < sizeof(GObject)) inst_size = sizeof(GObject);
    GObject *obj = (GObject*)g_malloc0(inst_size);
    obj->g_type_instance = type; obj->ref_count = 1;
    if (type < type_count && type_registry[type].instance_init)
        type_registry[type].instance_init(obj, NULL);
    return obj;
}
gpointer g_object_ref(gpointer obj) { if (obj) ((GObject*)obj)->ref_count++; return obj; }
void g_object_unref(gpointer obj) { if (obj && --((GObject*)obj)->ref_count == 0) g_free(obj); }
void g_object_set(gpointer obj, const gchar *p, ...) { (void)obj;(void)p; }
void g_object_get(gpointer obj, const gchar *p, ...) { (void)obj;(void)p; }
void g_object_set_data(GObject *obj, const gchar *key, gpointer data) { (void)obj;(void)key;(void)data; }
gpointer g_object_get_data(GObject *obj, const gchar *key) { (void)obj;(void)key; return NULL; }
void g_object_set_data_full(GObject *obj, const gchar *key, gpointer data, GDestroyNotify d) { (void)obj;(void)key;(void)data;(void)d; }

gulong g_signal_connect_data(gpointer inst, const gchar *sig, GCallback handler, gpointer data, GDestroyNotify dn, int flags) {
    (void)inst;(void)sig;(void)handler;(void)data;(void)dn;(void)flags; return 1;
}
void g_signal_emit_by_name(gpointer inst, const gchar *sig, ...) { (void)inst;(void)sig; }
void g_signal_handler_disconnect(gpointer inst, gulong hid) { (void)inst;(void)hid; }
gulong g_signal_handler_find(gpointer inst, int mask, guint sid, GQuark d, GClosure *c, gpointer f, gpointer da) {
    (void)inst;(void)mask;(void)sid;(void)d;(void)c;(void)f;(void)da; return 0;
}
guint g_signal_new(const gchar *name, GType itype, int flags, guint offset, void *acc, gpointer ad, void *marsh, GType ret, guint np, ...) {
    (void)name;(void)itype;(void)flags;(void)offset;(void)acc;(void)ad;(void)marsh;(void)ret;(void)np; static guint id=1; return id++;
}

GValue *g_value_init(GValue *v, GType t) { memset(v, 0, sizeof(*v)); v->g_type = t; return v; }
void g_value_unset(GValue *v) { if (v) v->g_type = 0; }
void g_value_set_int(GValue *v, gint i) { v->data[0].v_int = i; }
gint g_value_get_int(const GValue *v) { return v->data[0].v_int; }
void g_value_set_uint(GValue *v, guint u) { v->data[0].v_uint = u; }
guint g_value_get_uint(const GValue *v) { return v->data[0].v_uint; }
void g_value_set_boolean(GValue *v, gboolean b) { v->data[0].v_int = b; }
gboolean g_value_get_boolean(const GValue *v) { return v->data[0].v_int; }
void g_value_set_string(GValue *v, const gchar *s) { v->data[0].v_pointer = (gpointer)s; }
const gchar *g_value_get_string(const GValue *v) { return (const gchar*)v->data[0].v_pointer; }
void g_value_set_pointer(GValue *v, gpointer p) { v->data[0].v_pointer = p; }
gpointer g_value_get_pointer(const GValue *v) { return v->data[0].v_pointer; }
void g_value_set_object(GValue *v, gpointer o) { v->data[0].v_pointer = o; }
gpointer g_value_get_object(const GValue *v) { return v->data[0].v_pointer; }
void g_value_set_double(GValue *v, gdouble d) { v->data[0].v_double = d; }
gdouble g_value_get_double(const GValue *v) { return v->data[0].v_double; }
void g_value_set_float(GValue *v, gfloat f) { v->data[0].v_float = f; }
gfloat g_value_get_float(const GValue *v) { return v->data[0].v_float; }

typedef struct { GArray base; guint elem_size, alloc; gboolean zero_term, clear; } GArrayPriv;

GArray *g_array_new(gboolean zt, gboolean cl, guint es) { return g_array_sized_new(zt, cl, es, 16); }
GArray *g_array_sized_new(gboolean zt, gboolean cl, guint es, guint rs) {
    GArrayPriv *a = g_new0(GArrayPriv, 1);
    a->elem_size = es; a->alloc = rs; a->zero_term = zt; a->clear = cl;
    a->base.data = (gchar*)g_malloc0((gsize)es * rs + (zt ? es : 0));
    a->base.len = 0;
    return &a->base;
}
GArray *g_array_append_vals(GArray *arr, gconstpointer data, guint len) {
    GArrayPriv *a = (GArrayPriv*)arr;
    if (arr->len + len > a->alloc) { a->alloc = (arr->len + len) * 2;
        arr->data = (gchar*)g_realloc(arr->data, (gsize)a->elem_size * a->alloc + (a->zero_term ? a->elem_size : 0)); }
    memcpy(arr->data + (gsize)arr->len * a->elem_size, data, (gsize)len * a->elem_size);
    arr->len += len;
    if (a->zero_term) memset(arr->data + (gsize)arr->len * a->elem_size, 0, a->elem_size);
    return arr;
}
GArray *g_array_set_size(GArray *arr, guint length) {
    GArrayPriv *a = (GArrayPriv*)arr;
    if (length > a->alloc) { a->alloc = length * 2;
        arr->data = (gchar*)g_realloc(arr->data, (gsize)a->elem_size * a->alloc + (a->zero_term ? a->elem_size : 0)); }
    if (a->clear && length > arr->len) memset(arr->data + (gsize)arr->len * a->elem_size, 0, (gsize)(length - arr->len) * a->elem_size);
    arr->len = length;
    return arr;
}
gchar *g_array_free(GArray *arr, gboolean fs) { gchar *d = fs ? NULL : arr->data; if (fs) g_free(arr->data); g_free(arr); return d; }

GPtrArray *g_ptr_array_new(void) { return g_ptr_array_new_with_free_func(NULL); }
GPtrArray *g_ptr_array_new_with_free_func(GDestroyNotify f) {
    GPtrArray *a = g_new0(GPtrArray, 1);
    a->pdata = (gpointer*)g_malloc(16 * sizeof(gpointer)); a->len = 0;
    
    (void)f;
    return a;
}
void g_ptr_array_add(GPtrArray *a, gpointer data) {
    a->pdata = (gpointer*)g_realloc(a->pdata, (a->len + 1) * sizeof(gpointer));
    a->pdata[a->len++] = data;
}
gboolean g_ptr_array_remove(GPtrArray *a, gpointer data) {
    for (guint i = 0; i < a->len; i++) { if (a->pdata[i] == data) { g_ptr_array_remove_index(a, i); return TRUE; } }
    return FALSE;
}
gpointer g_ptr_array_remove_index(GPtrArray *a, guint i) {
    if (i >= a->len) return NULL;
    gpointer d = a->pdata[i];
    for (guint j = i; j < a->len - 1; j++) a->pdata[j] = a->pdata[j+1];
    a->len--;
    return d;
}
void g_ptr_array_sort(GPtrArray *a, GCompareFunc cmp) {
    for (guint i = 1; i < a->len; i++) {
        gpointer k = a->pdata[i]; guint j = i;
        while (j > 0 && cmp(&a->pdata[j-1], &k) > 0) { a->pdata[j] = a->pdata[j-1]; j--; }
        a->pdata[j] = k;
    }
}
gpointer *g_ptr_array_free(GPtrArray *a, gboolean fs) {
    gpointer *d = fs ? NULL : a->pdata; if (fs) g_free(a->pdata); g_free(a); return d;
}
void g_ptr_array_unref(GPtrArray *a) { if (a) { g_free(a->pdata); g_free(a); } }
void g_ptr_array_set_free_func(GPtrArray *a, GDestroyNotify f) { (void)a;(void)f; }

struct _GBytes { gconstpointer data; gsize size; guint ref_count; gboolean is_static; };
GBytes *g_bytes_new(gconstpointer d, gsize s) {
    GBytes *b = g_new(GBytes, 1); b->data = g_memdup2(d, s); b->size = s; b->ref_count = 1; b->is_static = FALSE; return b;
}
GBytes *g_bytes_new_static(gconstpointer d, gsize s) {
    GBytes *b = g_new(GBytes, 1); b->data = d; b->size = s; b->ref_count = 1; b->is_static = TRUE; return b;
}
GBytes *g_bytes_ref(GBytes *b) { b->ref_count++; return b; }
void g_bytes_unref(GBytes *b) { if (b && --b->ref_count == 0) { if (!b->is_static) g_free((gpointer)b->data); g_free(b); } }
gconstpointer g_bytes_get_data(GBytes *b, gsize *s) { if (s) *s = b->size; return b->data; }
gsize g_bytes_get_size(GBytes *b) { return b->size; }

struct _GParamSpec { GType value_type; const gchar *name; guint flags; guint ref_count; };

static GParamSpec *pspec_alloc(const gchar *name, guint flags) {
    GParamSpec *p = g_new0(GParamSpec, 1); p->name = g_strdup(name); p->flags = flags; p->ref_count = 1; return p;
}

GParamSpec *g_param_spec_int(const gchar *n,const gchar *nk,const gchar *b,gint mn,gint mx,gint d,guint f) { (void)nk;(void)b;(void)mn;(void)mx;(void)d; return pspec_alloc(n,f); }
GParamSpec *g_param_spec_uint(const gchar *n,const gchar *nk,const gchar *b,guint mn,guint mx,guint d,guint f) { (void)nk;(void)b;(void)mn;(void)mx;(void)d; return pspec_alloc(n,f); }
GParamSpec *g_param_spec_boolean(const gchar *n,const gchar *nk,const gchar *b,gboolean d,guint f) { (void)nk;(void)b;(void)d; return pspec_alloc(n,f); }
GParamSpec *g_param_spec_string(const gchar *n,const gchar *nk,const gchar *b,const gchar *d,guint f) { (void)nk;(void)b;(void)d; return pspec_alloc(n,f); }
GParamSpec *g_param_spec_double(const gchar *n,const gchar *nk,const gchar *b,gdouble mn,gdouble mx,gdouble d,guint f) { (void)nk;(void)b;(void)mn;(void)mx;(void)d; return pspec_alloc(n,f); }
GParamSpec *g_param_spec_float(const gchar *n,const gchar *nk,const gchar *b,gfloat mn,gfloat mx,gfloat d,guint f) { (void)nk;(void)b;(void)mn;(void)mx;(void)d; return pspec_alloc(n,f); }
GParamSpec *g_param_spec_object(const gchar *n,const gchar *nk,const gchar *b,GType ot,guint f) { (void)nk;(void)b;(void)ot; return pspec_alloc(n,f); }
GParamSpec *g_param_spec_enum(const gchar *n,const gchar *nk,const gchar *b,GType et,gint d,guint f) { (void)nk;(void)b;(void)et;(void)d; return pspec_alloc(n,f); }
GParamSpec *g_param_spec_pointer(const gchar *n,const gchar *nk,const gchar *b,guint f) { (void)nk;(void)b; return pspec_alloc(n,f); }

void g_object_class_install_property(GObjectClass *oc, guint pid, GParamSpec *ps) { (void)oc;(void)pid;(void)ps; }
void g_object_class_install_properties(GObjectClass *oc, guint n, GParamSpec **ps) { (void)oc;(void)n;(void)ps; }

gchar *g_utf8_next_char_ptr(const gchar *p) { return (gchar*)(p + g_utf8_skip[*(const guchar*)p]); }
guint32 g_utf8_get_char(const gchar *p) {
    guchar c = (guchar)*p;
    if (c < 0x80) return c;
    if (c < 0xE0) return ((c & 0x1F) << 6) | (p[1] & 0x3F);
    if (c < 0xF0) return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
}
glong g_utf8_strlen(const gchar *p, gssize max) {
    glong n = 0;
    const gchar *end = (max < 0) ? (const gchar*)(uintptr_t)-1 : p + max;
    while (p < end && *p) { p += g_utf8_skip[*(const guchar*)p]; n++; }
    return n;
}
gchar *g_utf8_offset_to_pointer(const gchar *s, glong off) { while (off-- > 0 && *s) s += g_utf8_skip[*(const guchar*)s]; return (gchar*)s; }
glong g_utf8_pointer_to_offset(const gchar *s, const gchar *pos) { glong n = 0; while (s < pos && *s) { s += g_utf8_skip[*(const guchar*)s]; n++; } return n; }
gboolean g_utf8_validate(const gchar *s, gssize max_len, const gchar **end) { (void)max_len; if (end) *end = s + strlen(s); return TRUE; }
gchar *g_utf8_strup(const gchar *s, gssize len) { return g_ascii_strup(s, len); }
gchar *g_utf8_strdown(const gchar *s, gssize len) { return g_ascii_strdown(s, len); }
gchar *g_utf8_casefold(const gchar *s, gssize len) { return g_ascii_strdown(s, len); }
gint g_utf8_collate(const gchar *s1, const gchar *s2) { return strcmp(s1, s2); }
guint32 *g_utf8_to_ucs4(const gchar *s, glong len, glong *ir, glong *iw, GError **e) { (void)s;(void)len;(void)ir;(void)iw;(void)e; return NULL; }
gchar *g_ucs4_to_utf8(const guint32 *s, glong len, glong *ir, glong *iw, GError **e) { (void)s;(void)len;(void)ir;(void)iw;(void)e; return NULL; }

const gchar *g_get_home_dir(void) { return "/"; }
const gchar *g_get_user_data_dir(void) { return "/"; }
const gchar *g_get_user_config_dir(void) { return "/"; }
const gchar *g_get_user_cache_dir(void) { return "/"; }
const gchar *g_get_tmp_dir(void) { return "/"; }

gchar *g_build_filename(const gchar *first, ...) {
    GString *s = g_string_new(first);
    va_list ap;
    va_start(ap, first);
    const gchar *p;
    while ((p = va_arg(ap, const gchar*)) != NULL) {
        if (s->len > 0 && s->str[s->len-1] != '/') g_string_append_c(s, '/');
        g_string_append(s, p);
    }
    va_end(ap);
    return g_string_free(s, FALSE);
}

gchar *g_path_get_dirname(const gchar *fn) {
    const char *s = strrchr(fn, '/');
    if (!s) return g_strdup(".");
    if (s == fn) return g_strdup("/");
    return g_strndup(fn, (gsize)(s - fn));
}

gchar *g_path_get_basename(const gchar *fn) {
    const char *s = strrchr(fn, '/');
    return g_strdup(s ? s + 1 : fn);
}

gboolean g_path_is_absolute(const gchar *fn) { return fn && fn[0] == '/'; }
gchar *g_get_current_dir(void) { return g_strdup("/"); }

gchar *g_base64_encode(const guchar *data, gsize len) { (void)data;(void)len; return g_strdup(""); }
guchar *g_base64_decode(const gchar *text, gsize *out_len) { (void)text; *out_len = 0; return (guchar*)g_strdup(""); }

void g_usleep(gulong us) { (void)us; }
gint64 g_get_monotonic_time(void) { return 0; }
gint64 g_get_real_time(void) { return 0; }

struct _GIOChannel { int fd; guint ref_count; };
GIOChannel *g_io_channel_unix_new(int fd) { GIOChannel *c = g_new0(GIOChannel, 1); c->fd = fd; c->ref_count = 1; return c; }
guint g_io_add_watch(GIOChannel *c, GIOCondition cond, GIOFunc func, gpointer ud) { (void)c;(void)cond;(void)func;(void)ud; return 1; }
void g_io_channel_unref(GIOChannel *c) { if (c && --c->ref_count == 0) g_free(c); }
