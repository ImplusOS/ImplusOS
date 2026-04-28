#pragma once

#include <stddef.h>
#include <stdint.h>

typedef unsigned long GType;
typedef int gboolean;
typedef unsigned int guint;
typedef int gint;
typedef void *gpointer;
typedef void (*GDestroyNotify)(gpointer data);

typedef struct _GdkDisplay     GdkDisplay;
typedef struct _GdkScreen      GdkScreen;
typedef struct _GdkWindow      GdkWindow;
typedef struct _GdkVisual      GdkVisual;
typedef struct _GdkMonitor     GdkMonitor;
typedef struct _GdkCursor      GdkCursor;
typedef struct _GdkEvent       GdkEvent;
typedef struct _GdkRGBA        GdkRGBA;
typedef struct _GdkRectangle   GdkRectangle;
typedef struct _GdkAtom_       *GdkAtom;

struct _GdkRGBA { double red, green, blue, alpha; };
struct _GdkRectangle { int x, y, width, height; };

typedef enum { GDK_WINDOW_TOPLEVEL=0, GDK_WINDOW_CHILD, GDK_WINDOW_TEMP } GdkWindowType;
typedef enum { GDK_INPUT_OUTPUT=0, GDK_INPUT_ONLY } GdkWindowWindowClass;
typedef enum { GDK_NOTHING=-1, GDK_DELETE=0, GDK_DESTROY=1, GDK_EXPOSE=2, GDK_MOTION_NOTIFY=3,
               GDK_BUTTON_PRESS=4, GDK_BUTTON_RELEASE=7, GDK_KEY_PRESS=8, GDK_KEY_RELEASE=9,
               GDK_ENTER_NOTIFY=10, GDK_LEAVE_NOTIFY=11, GDK_FOCUS_CHANGE=12, GDK_CONFIGURE=13,
               GDK_MAP=14, GDK_UNMAP=15 } GdkEventType;
typedef enum { GDK_EXPOSURE_MASK=(1<<1), GDK_POINTER_MOTION_MASK=(1<<2),
               GDK_BUTTON_PRESS_MASK=(1<<8), GDK_BUTTON_RELEASE_MASK=(1<<9),
               GDK_KEY_PRESS_MASK=(1<<10), GDK_KEY_RELEASE_MASK=(1<<11),
               GDK_STRUCTURE_MASK=(1<<16), GDK_ALL_EVENTS_MASK=0x3FFFFE } GdkEventMask;

#define GDK_NONE ((GdkAtom)0)

GdkDisplay *gdk_display_get_default(void);
const char *gdk_display_get_name(GdkDisplay *display);
GdkScreen  *gdk_display_get_default_screen(GdkDisplay *display);
int         gdk_display_get_n_monitors(GdkDisplay *display);
GdkMonitor *gdk_display_get_monitor(GdkDisplay *display, int monitor_num);
GdkMonitor *gdk_display_get_primary_monitor(GdkDisplay *display);
void        gdk_display_flush(GdkDisplay *display);
void        gdk_display_sync(GdkDisplay *display);

GdkScreen *gdk_screen_get_default(void);
int        gdk_screen_get_width(GdkScreen *screen);
int        gdk_screen_get_height(GdkScreen *screen);
GdkVisual *gdk_screen_get_rgba_visual(GdkScreen *screen);
GdkVisual *gdk_screen_get_system_visual(GdkScreen *screen);
GdkDisplay *gdk_screen_get_display(GdkScreen *screen);

void gdk_monitor_get_geometry(GdkMonitor *monitor, GdkRectangle *geometry);
int  gdk_monitor_get_scale_factor(GdkMonitor *monitor);

GdkWindow *gdk_get_default_root_window(void);
void       gdk_window_destroy(GdkWindow *window);
void       gdk_window_show(GdkWindow *window);
void       gdk_window_hide(GdkWindow *window);
void       gdk_window_move_resize(GdkWindow *window, int x, int y, int width, int height);
int        gdk_window_get_width(GdkWindow *window);
int        gdk_window_get_height(GdkWindow *window);
void       gdk_window_invalidate_rect(GdkWindow *window, const GdkRectangle *rect, gboolean invalidate_children);
GdkDisplay *gdk_window_get_display(GdkWindow *window);
GdkScreen  *gdk_window_get_screen(GdkWindow *window);
void       gdk_window_set_events(GdkWindow *window, GdkEventMask event_mask);

GdkCursor *gdk_cursor_new_from_name(GdkDisplay *display, const char *name);
void       gdk_cursor_unref(GdkCursor *cursor);

int gdk_visual_get_depth(GdkVisual *visual);

void gdk_init(int *argc, char ***argv);
void gdk_init_check(int *argc, char ***argv);

void gdk_event_free(GdkEvent *event);

GdkAtom gdk_atom_intern(const char *atom_name, gboolean only_if_exists);
const char *gdk_atom_name(GdkAtom atom);

typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;
cairo_t *gdk_cairo_create(GdkWindow *window);
void     gdk_cairo_set_source_rgba(cairo_t *cr, const GdkRGBA *rgba);

GType gdk_display_get_type(void);
GType gdk_screen_get_type(void);
GType gdk_window_get_type(void);
