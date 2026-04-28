#include "dbus.h"
#include <string.h>

extern void *calloc(unsigned long, unsigned long);
extern void  free(void *);

struct DBusError { const char *name; const char *message; int is_set; };
struct DBusConnection { int ref_count; };
struct DBusMessage { int type; char member[64]; char iface[64]; char path[128]; int ref_count; };

static struct DBusConnection g_conn = { .ref_count = 1 };

void dbus_error_init(DBusError *e) { if (e) memset(e, 0, sizeof(*e)); }
void dbus_error_free(DBusError *e) { if (e) e->is_set = 0; }
dbus_bool_t dbus_error_is_set(const DBusError *e) { return e ? e->is_set : 0; }
const char *dbus_error_name(const DBusError *e) { return e && e->name ? e->name : ""; }

DBusConnection *dbus_bus_get(DBusBusType t, DBusError *e) { (void)t;(void)e; g_conn.ref_count++; return &g_conn; }
DBusConnection *dbus_bus_get_private(DBusBusType t, DBusError *e) { return dbus_bus_get(t, e); }
void dbus_connection_unref(DBusConnection *c) { if (c && c != &g_conn) free(c); }
dbus_bool_t dbus_connection_send(DBusConnection *c, DBusMessage *m, dbus_uint32_t *s) { (void)c;(void)m; if(s) *s=1; return 1; }
dbus_bool_t dbus_connection_send_with_reply(DBusConnection *c, DBusMessage *m, DBusPendingCall **p, int t) { (void)c;(void)m;(void)t; if(p) *p=NULL; return 1; }
dbus_bool_t dbus_connection_send_with_reply_and_block(DBusConnection *c, DBusMessage *m, int t, DBusError *e) { (void)c;(void)m;(void)t;(void)e; return 1; }
void dbus_connection_flush(DBusConnection *c) { (void)c; }
dbus_bool_t dbus_connection_read_write(DBusConnection *c, int t) { (void)c;(void)t; return 1; }
dbus_bool_t dbus_connection_read_write_dispatch(DBusConnection *c, int t) { (void)c;(void)t; return 1; }
DBusMessage *dbus_connection_pop_message(DBusConnection *c) { (void)c; return NULL; }

DBusMessage *dbus_message_new_method_call(const char *dest, const char *path, const char *iface, const char *method) {
    (void)dest;
    DBusMessage *m = (DBusMessage*)calloc(1, sizeof(*m));
    m->type = DBUS_MESSAGE_TYPE_METHOD_CALL; m->ref_count = 1;
    if (method) strncpy(m->member, method, 63);
    if (iface)  strncpy(m->iface, iface, 63);
    if (path)   strncpy(m->path, path, 127);
    return m;
}

DBusMessage *dbus_message_new_signal(const char *path, const char *iface, const char *name) {
    DBusMessage *m = (DBusMessage*)calloc(1, sizeof(*m));
    m->type = DBUS_MESSAGE_TYPE_SIGNAL; m->ref_count = 1;
    if (name)  strncpy(m->member, name, 63);
    if (iface) strncpy(m->iface, iface, 63);
    if (path)  strncpy(m->path, path, 127);
    return m;
}

DBusMessage *dbus_message_new_method_return(DBusMessage *mc) {
    (void)mc;
    DBusMessage *m = (DBusMessage*)calloc(1, sizeof(*m));
    m->type = DBUS_MESSAGE_TYPE_METHOD_RETURN; m->ref_count = 1;
    return m;
}

void dbus_message_unref(DBusMessage *m) { if (m && --m->ref_count <= 0) free(m); }
int dbus_message_get_type(DBusMessage *m) { return m ? m->type : 0; }
const char *dbus_message_get_member(DBusMessage *m) { return m ? m->member : NULL; }
const char *dbus_message_get_interface(DBusMessage *m) { return m ? m->iface : NULL; }
const char *dbus_message_get_path(DBusMessage *m) { return m ? m->path : NULL; }

dbus_bool_t dbus_threads_init_default(void) { return 1; }
