#include "gdk/gdk.h"
#include <string.h>

extern void *calloc(unsigned long, unsigned long);
extern void  free(void *);

struct _GdkDisplay { int ref_count; };
struct _GdkScreen  { int width, height; GdkDisplay *display; };
struct _GdkWindow  { int x, y, width, height; GdkEventMask events; GdkDisplay *display; GdkScreen *screen; };
struct _GdkVisual  { int depth; };
struct _GdkMonitor { GdkRectangle geometry; int scale_factor; };
struct _GdkCursor  { int ref_count; };
struct _GdkEvent   { GdkEventType type; };

static GdkDisplay g_display = { .ref_count = 1 };
static GdkScreen  g_screen  = { .width = 1024, .height = 768, .display = &g_display };
static GdkWindow  g_root    = { .x=0, .y=0, .width=1024, .height=768, .display=&g_display, .screen=&g_screen };
static GdkVisual  g_visual  = { .depth = 32 };
static GdkMonitor g_monitor = { .geometry = {0,0,1024,768}, .scale_factor = 1 };

GdkDisplay *gdk_display_get_default(void) { return &g_display; }
const char *gdk_display_get_name(GdkDisplay *d) { (void)d; return "ImplusOS:0"; }
GdkScreen  *gdk_display_get_default_screen(GdkDisplay *d) { (void)d; return &g_screen; }
int         gdk_display_get_n_monitors(GdkDisplay *d) { (void)d; return 1; }
GdkMonitor *gdk_display_get_monitor(GdkDisplay *d, int n) { (void)d;(void)n; return &g_monitor; }
GdkMonitor *gdk_display_get_primary_monitor(GdkDisplay *d) { (void)d; return &g_monitor; }
void        gdk_display_flush(GdkDisplay *d) { (void)d; }
void        gdk_display_sync(GdkDisplay *d) { (void)d; }

GdkScreen *gdk_screen_get_default(void) { return &g_screen; }
int        gdk_screen_get_width(GdkScreen *s) { return s->width; }
int        gdk_screen_get_height(GdkScreen *s) { return s->height; }
GdkVisual *gdk_screen_get_rgba_visual(GdkScreen *s) { (void)s; return &g_visual; }
GdkVisual *gdk_screen_get_system_visual(GdkScreen *s) { (void)s; return &g_visual; }
GdkDisplay *gdk_screen_get_display(GdkScreen *s) { return s->display; }

void gdk_monitor_get_geometry(GdkMonitor *m, GdkRectangle *g) { if(g) *g = m->geometry; }
int  gdk_monitor_get_scale_factor(GdkMonitor *m) { return m->scale_factor; }

GdkWindow *gdk_get_default_root_window(void) { return &g_root; }
void gdk_window_destroy(GdkWindow *w) { if (w && w != &g_root) free(w); }
void gdk_window_show(GdkWindow *w) { (void)w; }
void gdk_window_hide(GdkWindow *w) { (void)w; }
void gdk_window_move_resize(GdkWindow *w, int x, int y, int width, int height) { w->x=x; w->y=y; w->width=width; w->height=height; }
int  gdk_window_get_width(GdkWindow *w) { return w->width; }
int  gdk_window_get_height(GdkWindow *w) { return w->height; }
void gdk_window_invalidate_rect(GdkWindow *w, const GdkRectangle *r, gboolean ic) { (void)w;(void)r;(void)ic; }
GdkDisplay *gdk_window_get_display(GdkWindow *w) { return w->display; }
GdkScreen  *gdk_window_get_screen(GdkWindow *w) { return w->screen; }
void gdk_window_set_events(GdkWindow *w, GdkEventMask m) { w->events = m; }

GdkCursor *gdk_cursor_new_from_name(GdkDisplay *d, const char *n) { (void)d;(void)n; GdkCursor *c = (GdkCursor*)calloc(1,sizeof(*c)); c->ref_count=1; return c; }
void gdk_cursor_unref(GdkCursor *c) { if(c && --c->ref_count<=0) free(c); }

int gdk_visual_get_depth(GdkVisual *v) { return v->depth; }

void gdk_init(int *argc, char ***argv) { (void)argc;(void)argv; }
void gdk_init_check(int *argc, char ***argv) { (void)argc;(void)argv; }
void gdk_event_free(GdkEvent *e) { free(e); }

struct _GdkAtom_ { const char *name; };
static struct _GdkAtom_ g_atoms[64];
static int g_atom_count = 0;

GdkAtom gdk_atom_intern(const char *name, gboolean oie) {
    (void)oie;
    for (int i = 0; i < g_atom_count; i++)
        if (strcmp(g_atoms[i].name, name) == 0) return (GdkAtom)&g_atoms[i];
    if (g_atom_count < 64) { g_atoms[g_atom_count].name = name; return (GdkAtom)&g_atoms[g_atom_count++]; }
    return GDK_NONE;
}
const char *gdk_atom_name(GdkAtom a) { return a ? ((struct _GdkAtom_*)a)->name : NULL; }

cairo_t *gdk_cairo_create(GdkWindow *w) { (void)w; return NULL; }
void  gdk_cairo_set_source_rgba(cairo_t *cr, const GdkRGBA *rgba) { (void)cr;(void)rgba; }

GType gdk_display_get_type(void) { return 400; }
GType gdk_screen_get_type(void) { return 401; }
GType gdk_window_get_type(void) { return 402; }
