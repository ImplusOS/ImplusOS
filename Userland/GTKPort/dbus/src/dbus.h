#pragma once

#include <stddef.h>
#include <stdint.h>

typedef int dbus_bool_t;
typedef uint32_t dbus_uint32_t;

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage    DBusMessage;
typedef struct DBusError      DBusError;
typedef struct DBusPendingCall DBusPendingCall;

typedef enum { DBUS_BUS_SESSION=0, DBUS_BUS_SYSTEM, DBUS_BUS_STARTER } DBusBusType;

#define DBUS_TYPE_INVALID   0
#define DBUS_TYPE_STRING    's'
#define DBUS_TYPE_INT32     'i'
#define DBUS_TYPE_UINT32    'u'
#define DBUS_TYPE_BOOLEAN   'b'
#define DBUS_TYPE_ARRAY     'a'
#define DBUS_TYPE_VARIANT   'v'

#define DBUS_MESSAGE_TYPE_METHOD_CALL   1
#define DBUS_MESSAGE_TYPE_METHOD_RETURN 2
#define DBUS_MESSAGE_TYPE_ERROR         3
#define DBUS_MESSAGE_TYPE_SIGNAL        4

void           dbus_error_init(DBusError *error);
void           dbus_error_free(DBusError *error);
dbus_bool_t    dbus_error_is_set(const DBusError *error);
const char    *dbus_error_name(const DBusError *error);

DBusConnection *dbus_bus_get(DBusBusType type, DBusError *error);
DBusConnection *dbus_bus_get_private(DBusBusType type, DBusError *error);
void            dbus_connection_unref(DBusConnection *connection);
dbus_bool_t     dbus_connection_send(DBusConnection *connection, DBusMessage *message, dbus_uint32_t *serial);
dbus_bool_t     dbus_connection_send_with_reply(DBusConnection *connection, DBusMessage *message, DBusPendingCall **pending, int timeout_ms);
dbus_bool_t     dbus_connection_send_with_reply_and_block(DBusConnection *connection, DBusMessage *message, int timeout_ms, DBusError *error);
void            dbus_connection_flush(DBusConnection *connection);
dbus_bool_t     dbus_connection_read_write(DBusConnection *connection, int timeout_ms);
dbus_bool_t     dbus_connection_read_write_dispatch(DBusConnection *connection, int timeout_ms);
DBusMessage    *dbus_connection_pop_message(DBusConnection *connection);

DBusMessage *dbus_message_new_method_call(const char *destination, const char *path, const char *iface, const char *method);
DBusMessage *dbus_message_new_signal(const char *path, const char *iface, const char *name);
DBusMessage *dbus_message_new_method_return(DBusMessage *method_call);
void         dbus_message_unref(DBusMessage *message);
int          dbus_message_get_type(DBusMessage *message);
const char  *dbus_message_get_member(DBusMessage *message);
const char  *dbus_message_get_interface(DBusMessage *message);
const char  *dbus_message_get_path(DBusMessage *message);

dbus_bool_t dbus_threads_init_default(void);
