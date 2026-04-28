#include "gtk/gtk.h"
#include <string.h>
#include <stdarg.h>

extern void *calloc(unsigned long, unsigned long);
extern void *malloc(unsigned long);
extern void  free(void *);
extern char *strdup(const char *);
extern int   snprintf(char *, unsigned long, const char *, ...);

typedef struct _WidgetBase {
    GType type;
    char name[64];
    char tooltip[128];
    int x, y, width, height;
    int req_width, req_height;
    gboolean visible, sensitive;
    GtkWidget *parent;
    GdkWindow *gdk_window;
    int ref_count;
    
    GtkWidget *children[32];
    int n_children;
    guint border_width;
    
    char title[128];
    char label_text[256];
    char entry_text[256];
    char placeholder[128];
    GtkOrientation orientation;
    int spacing;
    gboolean entry_visible;
    int entry_max;
    GtkWindowType win_type;
    GtkApplication *app;
} WidgetBase;

static int g_gtk_running = 0;

static WidgetBase *widget_alloc(GType t) {
    WidgetBase *w = (WidgetBase*)calloc(1, sizeof(*w));
    w->type = t; w->visible = 0; w->sensitive = 1; w->ref_count = 1;
    w->width = 100; w->height = 50;
    w->entry_visible = 1;
    return w;
}

void gtk_init(int *argc, char ***argv) { (void)argc;(void)argv; gdk_init(argc, argv); }
gboolean gtk_init_check(int *argc, char ***argv) { gtk_init(argc, argv); return 1; }

void gtk_main(void) { g_gtk_running = 1; while (g_gtk_running) {  break; } }
void gtk_main_quit(void) { g_gtk_running = 0; }
gboolean gtk_main_iteration(void) { return 0; }
gboolean gtk_events_pending(void) { return 0; }

GType gtk_widget_get_type(void) { return 500; }
void  gtk_widget_show(GtkWidget *w) { ((WidgetBase*)w)->visible = 1; }
void  gtk_widget_show_all(GtkWidget *w) {
    WidgetBase *wb = (WidgetBase*)w;
    wb->visible = 1;
    for (int i = 0; i < wb->n_children; i++)
        gtk_widget_show_all(wb->children[i]);
}
void  gtk_widget_hide(GtkWidget *w) { ((WidgetBase*)w)->visible = 0; }
void  gtk_widget_destroy(GtkWidget *w) { if (w) free(w); }
void  gtk_widget_set_sensitive(GtkWidget *w, gboolean s) { ((WidgetBase*)w)->sensitive = s; }
gboolean gtk_widget_get_sensitive(GtkWidget *w) { return ((WidgetBase*)w)->sensitive; }
void  gtk_widget_set_visible(GtkWidget *w, gboolean v) { ((WidgetBase*)w)->visible = v; }
gboolean gtk_widget_get_visible(GtkWidget *w) { return ((WidgetBase*)w)->visible; }
void  gtk_widget_queue_draw(GtkWidget *w) { (void)w; }
void  gtk_widget_queue_resize(GtkWidget *w) { (void)w; }
void  gtk_widget_set_size_request(GtkWidget *w, int width, int height) { WidgetBase *wb=(WidgetBase*)w; wb->req_width=width; wb->req_height=height; }
void  gtk_widget_get_allocation(GtkWidget *w, GtkAllocation *a) { WidgetBase *wb=(WidgetBase*)w; if(a){a->x=wb->x;a->y=wb->y;a->width=wb->width;a->height=wb->height;} }
GdkWindow  *gtk_widget_get_window(GtkWidget *w) { return ((WidgetBase*)w)->gdk_window; }
GdkDisplay *gtk_widget_get_display(GtkWidget *w) { (void)w; return gdk_display_get_default(); }
GdkScreen  *gtk_widget_get_screen(GtkWidget *w) { (void)w; return gdk_screen_get_default(); }
GtkStyleContext *gtk_widget_get_style_context(GtkWidget *w) { (void)w; return NULL; }
void  gtk_widget_set_halign(GtkWidget *w, GtkAlign a) { (void)w;(void)a; }
void  gtk_widget_set_valign(GtkWidget *w, GtkAlign a) { (void)w;(void)a; }
void  gtk_widget_set_hexpand(GtkWidget *w, gboolean e) { (void)w;(void)e; }
void  gtk_widget_set_vexpand(GtkWidget *w, gboolean e) { (void)w;(void)e; }
void  gtk_widget_set_margin_start(GtkWidget *w, int m)  { (void)w;(void)m; }
void  gtk_widget_set_margin_end(GtkWidget *w, int m)    { (void)w;(void)m; }
void  gtk_widget_set_margin_top(GtkWidget *w, int m)    { (void)w;(void)m; }
void  gtk_widget_set_margin_bottom(GtkWidget *w, int m) { (void)w;(void)m; }
void  gtk_widget_set_name(GtkWidget *w, const char *n) { if(n) strncpy(((WidgetBase*)w)->name, n, 63); }
const char *gtk_widget_get_name(GtkWidget *w) { return ((WidgetBase*)w)->name; }
void  gtk_widget_set_tooltip_text(GtkWidget *w, const char *t) { if(t) strncpy(((WidgetBase*)w)->tooltip, t, 127); }
void  gtk_widget_grab_focus(GtkWidget *w) { (void)w; }
void  gtk_widget_override_background_color(GtkWidget *w, GtkStateFlags s, const GdkRGBA *c) { (void)w;(void)s;(void)c; }
void  gtk_widget_override_color(GtkWidget *w, GtkStateFlags s, const GdkRGBA *c) { (void)w;(void)s;(void)c; }
GtkWidget *gtk_widget_get_parent(GtkWidget *w) { return ((WidgetBase*)w)->parent; }
GtkWidget *gtk_widget_get_toplevel(GtkWidget *w) {
    while (((WidgetBase*)w)->parent) w = ((WidgetBase*)w)->parent;
    return w;
}

GType gtk_container_get_type(void) { return 501; }
void  gtk_container_add(GtkContainer *c, GtkWidget *w) {
    WidgetBase *cb = (WidgetBase*)c;
    if (cb->n_children < 32) { cb->children[cb->n_children++] = w; ((WidgetBase*)w)->parent = (GtkWidget*)c; }
}
void gtk_container_remove(GtkContainer *c, GtkWidget *w) {
    WidgetBase *cb = (WidgetBase*)c;
    for (int i = 0; i < cb->n_children; i++) {
        if (cb->children[i] == w) {
            for (int j = i; j < cb->n_children - 1; j++) cb->children[j] = cb->children[j+1];
            cb->n_children--;
            ((WidgetBase*)w)->parent = NULL;
            return;
        }
    }
}
void gtk_container_set_border_width(GtkContainer *c, guint bw) { ((WidgetBase*)c)->border_width = bw; }
void gtk_container_foreach(GtkContainer *c, GtkCallback cb, gpointer d) {
    WidgetBase *cw = (WidgetBase*)c;
    for (int i = 0; i < cw->n_children; i++) cb(cw->children[i], d);
}

GType gtk_window_get_type(void) { return 502; }
GtkWidget *gtk_window_new(GtkWindowType type) { WidgetBase *w = widget_alloc(502); w->win_type = type; w->width=800; w->height=600; strcpy(w->title,"Window"); return (GtkWidget*)w; }
void gtk_window_set_title(GtkWindow *w, const char *t) { if(t) strncpy(((WidgetBase*)w)->title, t, 127); }
const char *gtk_window_get_title(GtkWindow *w) { return ((WidgetBase*)w)->title; }
void gtk_window_set_default_size(GtkWindow *w, int width, int height) { WidgetBase *wb=(WidgetBase*)w; wb->width=width; wb->height=height; }
void gtk_window_set_position(GtkWindow *w, int pos) { (void)w;(void)pos; }
void gtk_window_set_resizable(GtkWindow *w, gboolean r) { (void)w;(void)r; }
void gtk_window_set_decorated(GtkWindow *w, gboolean s) { (void)w;(void)s; }
void gtk_window_present(GtkWindow *w) { ((WidgetBase*)w)->visible = 1; }
void gtk_window_maximize(GtkWindow *w) { WidgetBase *wb=(WidgetBase*)w; wb->width=1024; wb->height=768; }
void gtk_window_fullscreen(GtkWindow *w) { gtk_window_maximize(w); }
void gtk_window_set_icon_name(GtkWindow *w, const char *n) { (void)w;(void)n; }

GType gtk_box_get_type(void) { return 503; }
GtkWidget *gtk_box_new(GtkOrientation o, int s) { WidgetBase *w = widget_alloc(503); w->orientation=o; w->spacing=s; return (GtkWidget*)w; }
void gtk_box_pack_start(GtkBox *b, GtkWidget *c, gboolean expand, gboolean fill, guint pad) { (void)expand;(void)fill;(void)pad; gtk_container_add((GtkContainer*)b, c); }
void gtk_box_pack_end(GtkBox *b, GtkWidget *c, gboolean expand, gboolean fill, guint pad) { (void)expand;(void)fill;(void)pad; gtk_container_add((GtkContainer*)b, c); }
void gtk_box_set_spacing(GtkBox *b, int s) { ((WidgetBase*)b)->spacing = s; }
void gtk_box_set_homogeneous(GtkBox *b, gboolean h) { (void)b;(void)h; }

GType gtk_label_get_type(void) { return 504; }
GtkWidget *gtk_label_new(const char *str) { WidgetBase *w = widget_alloc(504); if(str) strncpy(w->label_text, str, 255); return (GtkWidget*)w; }
GtkWidget *gtk_label_new_with_mnemonic(const char *str) { return gtk_label_new(str); }
void gtk_label_set_text(GtkLabel *l, const char *s) { if(s) strncpy(((WidgetBase*)l)->label_text, s, 255); }
const char *gtk_label_get_text(GtkLabel *l) { return ((WidgetBase*)l)->label_text; }
void gtk_label_set_markup(GtkLabel *l, const char *s) { gtk_label_set_text(l, s); }
void gtk_label_set_justify(GtkLabel *l, GtkJustification j) { (void)l;(void)j; }
void gtk_label_set_line_wrap(GtkLabel *l, gboolean w) { (void)l;(void)w; }
void gtk_label_set_selectable(GtkLabel *l, gboolean s) { (void)l;(void)s; }
void gtk_label_set_xalign(GtkLabel *l, float x) { (void)l;(void)x; }
void gtk_label_set_yalign(GtkLabel *l, float y) { (void)l;(void)y; }

GType gtk_button_get_type(void) { return 505; }
GtkWidget *gtk_button_new(void) { return (GtkWidget*)widget_alloc(505); }
GtkWidget *gtk_button_new_with_label(const char *l) { WidgetBase *w = widget_alloc(505); if(l) strncpy(w->label_text, l, 255); return (GtkWidget*)w; }
GtkWidget *gtk_button_new_with_mnemonic(const char *l) { return gtk_button_new_with_label(l); }
void gtk_button_set_label(GtkButton *b, const char *l) { if(l) strncpy(((WidgetBase*)b)->label_text, l, 255); }
const char *gtk_button_get_label(GtkButton *b) { return ((WidgetBase*)b)->label_text; }
void gtk_button_set_relief(GtkButton *b, int r) { (void)b;(void)r; }

GType gtk_entry_get_type(void) { return 506; }
GtkWidget *gtk_entry_new(void) { return (GtkWidget*)widget_alloc(506); }
void gtk_entry_set_text(GtkEntry *e, const char *t) { if(t) strncpy(((WidgetBase*)e)->entry_text, t, 255); }
const char *gtk_entry_get_text(GtkEntry *e) { return ((WidgetBase*)e)->entry_text; }
void gtk_entry_set_placeholder_text(GtkEntry *e, const char *t) { if(t) strncpy(((WidgetBase*)e)->placeholder, t, 127); }
void gtk_entry_set_visibility(GtkEntry *e, gboolean v) { ((WidgetBase*)e)->entry_visible = v; }
void gtk_entry_set_max_length(GtkEntry *e, int m) { ((WidgetBase*)e)->entry_max = m; }

struct _GtkApplication { char id[128]; int flags; int ref_count; };
GType gtk_application_get_type(void) { return 510; }
GtkApplication *gtk_application_new(const char *id, int flags) {
    GtkApplication *a = (GtkApplication*)calloc(1, sizeof(*a));
    if(id) strncpy(a->id, id, 127); a->flags = flags; a->ref_count = 1;
    return a;
}
int g_application_run(GtkApplication *app, int argc, char **argv) { (void)app;(void)argc;(void)argv; return 0; }
void g_application_quit(GtkApplication *app) { (void)app; gtk_main_quit(); }
GtkWidget *gtk_application_window_new(GtkApplication *app) {
    GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    ((WidgetBase*)w)->app = app;
    return w;
}

struct _GtkSettings { int dummy; };
static struct _GtkSettings g_settings;
GtkSettings *gtk_settings_get_default(void) { return &g_settings; }
GtkSettings *gtk_settings_get_for_screen(GdkScreen *s) { (void)s; return &g_settings; }

struct _GtkCssProvider { int ref_count; };
struct _GtkStyleContext { int dummy; };
GtkCssProvider *gtk_css_provider_new(void) { GtkCssProvider *p = (GtkCssProvider*)calloc(1,sizeof(*p)); p->ref_count=1; return p; }
gboolean gtk_css_provider_load_from_data(GtkCssProvider *p, const char *d, long l, void **e) { (void)p;(void)d;(void)l;(void)e; return 1; }
void gtk_style_context_add_provider_for_screen(GdkScreen *s, gpointer p, guint pri) { (void)s;(void)p;(void)pri; }
void gtk_style_context_add_class(GtkStyleContext *c, const char *cn) { (void)c;(void)cn; }
void gtk_style_context_remove_class(GtkStyleContext *c, const char *cn) { (void)c;(void)cn; }

unsigned long g_signal_connect_data(gpointer inst, const char *sig, GCallback handler, gpointer data, GDestroyNotify dn, int flags) {
    (void)inst;(void)sig;(void)handler;(void)data;(void)dn;(void)flags;
    return 1;
}

GType gtk_drawing_area_get_type(void) { return 507; }
GtkWidget *gtk_drawing_area_new(void) { return (GtkWidget*)widget_alloc(507); }

GType gtk_grid_get_type(void) { return 508; }
GtkWidget *gtk_grid_new(void) { return (GtkWidget*)widget_alloc(508); }
void gtk_grid_attach(void *g, GtkWidget *c, int l, int t, int w, int h) { (void)l;(void)t;(void)w;(void)h; gtk_container_add((GtkContainer*)g, c); }
void gtk_grid_set_row_spacing(void *g, guint s) { (void)g;(void)s; }
void gtk_grid_set_column_spacing(void *g, guint s) { (void)g;(void)s; }

GType gtk_scrolled_window_get_type(void) { return 509; }
GtkWidget *gtk_scrolled_window_new(void *h, void *v) { (void)h;(void)v; return (GtkWidget*)widget_alloc(509); }
void gtk_scrolled_window_set_policy(void *sw, int hp, int vp) { (void)sw;(void)hp;(void)vp; }

GtkWidget *gtk_message_dialog_new(GtkWindow *parent, int flags, int type, int buttons, const char *fmt, ...) {
    (void)parent;(void)flags;(void)type;(void)buttons;(void)fmt;
    return gtk_window_new(GTK_WINDOW_POPUP);
}
int gtk_dialog_run(void *dialog) { (void)dialog; return -4;  }
