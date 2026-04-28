#pragma once

#include "../gdk/gdk.h"

typedef struct _GtkWidget      GtkWidget;
typedef struct _GtkWindow      GtkWindow;
typedef struct _GtkContainer   GtkContainer;
typedef struct _GtkBin         GtkBin;
typedef struct _GtkBox         GtkBox;
typedef struct _GtkLabel       GtkLabel;
typedef struct _GtkButton      GtkButton;
typedef struct _GtkEntry       GtkEntry;
typedef struct _GtkApplication GtkApplication;
typedef struct _GtkSettings    GtkSettings;
typedef struct _GtkStyleContext GtkStyleContext;
typedef struct _GtkCssProvider GtkCssProvider;

typedef struct _GtkAllocation { int x, y, width, height; } GtkAllocation;

typedef void (*GCallback)(void);
typedef void (*GtkCallback)(GtkWidget *widget, gpointer data);

typedef enum { GTK_WINDOW_TOPLEVEL=0, GTK_WINDOW_POPUP } GtkWindowType;
typedef enum { GTK_ORIENTATION_HORIZONTAL=0, GTK_ORIENTATION_VERTICAL } GtkOrientation;
typedef enum { GTK_ALIGN_FILL=0, GTK_ALIGN_START, GTK_ALIGN_END, GTK_ALIGN_CENTER, GTK_ALIGN_BASELINE } GtkAlign;
typedef enum { GTK_PACK_START=0, GTK_PACK_END } GtkPackType;
typedef enum { GTK_JUSTIFY_LEFT=0, GTK_JUSTIFY_RIGHT, GTK_JUSTIFY_CENTER, GTK_JUSTIFY_FILL } GtkJustification;
typedef enum { GTK_ICON_SIZE_INVALID=0, GTK_ICON_SIZE_MENU=1, GTK_ICON_SIZE_BUTTON=4, GTK_ICON_SIZE_DIALOG=6 } GtkIconSize;
typedef enum { GTK_STATE_FLAG_NORMAL=0, GTK_STATE_FLAG_ACTIVE=(1<<0), GTK_STATE_FLAG_PRELIGHT=(1<<1),
               GTK_STATE_FLAG_SELECTED=(1<<2), GTK_STATE_FLAG_INSENSITIVE=(1<<3), GTK_STATE_FLAG_FOCUSED=(1<<12) } GtkStateFlags;
typedef enum { GTK_STYLE_PROVIDER_PRIORITY_USER=800, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION=600 } GtkStyleProviderPriority;

void     gtk_init(int *argc, char ***argv);
gboolean gtk_init_check(int *argc, char ***argv);

void gtk_main(void);
void gtk_main_quit(void);
gboolean gtk_main_iteration(void);
gboolean gtk_events_pending(void);

GType       gtk_widget_get_type(void);
void        gtk_widget_show(GtkWidget *widget);
void        gtk_widget_show_all(GtkWidget *widget);
void        gtk_widget_hide(GtkWidget *widget);
void        gtk_widget_destroy(GtkWidget *widget);
void        gtk_widget_set_sensitive(GtkWidget *widget, gboolean sensitive);
gboolean    gtk_widget_get_sensitive(GtkWidget *widget);
void        gtk_widget_set_visible(GtkWidget *widget, gboolean visible);
gboolean    gtk_widget_get_visible(GtkWidget *widget);
void        gtk_widget_queue_draw(GtkWidget *widget);
void        gtk_widget_queue_resize(GtkWidget *widget);
void        gtk_widget_set_size_request(GtkWidget *widget, int width, int height);
void        gtk_widget_get_allocation(GtkWidget *widget, GtkAllocation *allocation);
GdkWindow  *gtk_widget_get_window(GtkWidget *widget);
GdkDisplay *gtk_widget_get_display(GtkWidget *widget);
GdkScreen  *gtk_widget_get_screen(GtkWidget *widget);
GtkStyleContext *gtk_widget_get_style_context(GtkWidget *widget);
void        gtk_widget_set_halign(GtkWidget *widget, GtkAlign align);
void        gtk_widget_set_valign(GtkWidget *widget, GtkAlign align);
void        gtk_widget_set_hexpand(GtkWidget *widget, gboolean expand);
void        gtk_widget_set_vexpand(GtkWidget *widget, gboolean expand);
void        gtk_widget_set_margin_start(GtkWidget *widget, int margin);
void        gtk_widget_set_margin_end(GtkWidget *widget, int margin);
void        gtk_widget_set_margin_top(GtkWidget *widget, int margin);
void        gtk_widget_set_margin_bottom(GtkWidget *widget, int margin);
void        gtk_widget_set_name(GtkWidget *widget, const char *name);
const char *gtk_widget_get_name(GtkWidget *widget);
void        gtk_widget_set_tooltip_text(GtkWidget *widget, const char *text);
void        gtk_widget_grab_focus(GtkWidget *widget);
void        gtk_widget_override_background_color(GtkWidget *widget, GtkStateFlags state, const GdkRGBA *color);
void        gtk_widget_override_color(GtkWidget *widget, GtkStateFlags state, const GdkRGBA *color);
GtkWidget  *gtk_widget_get_parent(GtkWidget *widget);
GtkWidget  *gtk_widget_get_toplevel(GtkWidget *widget);

GType gtk_container_get_type(void);
void  gtk_container_add(GtkContainer *container, GtkWidget *widget);
void  gtk_container_remove(GtkContainer *container, GtkWidget *widget);
void  gtk_container_set_border_width(GtkContainer *container, guint border_width);
void  gtk_container_foreach(GtkContainer *container, GtkCallback callback, gpointer callback_data);

GType       gtk_window_get_type(void);
GtkWidget  *gtk_window_new(GtkWindowType type);
void        gtk_window_set_title(GtkWindow *window, const char *title);
const char *gtk_window_get_title(GtkWindow *window);
void        gtk_window_set_default_size(GtkWindow *window, int width, int height);
void        gtk_window_set_position(GtkWindow *window, int position);
void        gtk_window_set_resizable(GtkWindow *window, gboolean resizable);
void        gtk_window_set_decorated(GtkWindow *window, gboolean setting);
void        gtk_window_present(GtkWindow *window);
void        gtk_window_maximize(GtkWindow *window);
void        gtk_window_fullscreen(GtkWindow *window);
void        gtk_window_set_icon_name(GtkWindow *window, const char *name);

GType      gtk_box_get_type(void);
GtkWidget *gtk_box_new(GtkOrientation orientation, int spacing);
void       gtk_box_pack_start(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding);
void       gtk_box_pack_end(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding);
void       gtk_box_set_spacing(GtkBox *box, int spacing);
void       gtk_box_set_homogeneous(GtkBox *box, gboolean homogeneous);

GType      gtk_label_get_type(void);
GtkWidget *gtk_label_new(const char *str);
GtkWidget *gtk_label_new_with_mnemonic(const char *str);
void       gtk_label_set_text(GtkLabel *label, const char *str);
const char *gtk_label_get_text(GtkLabel *label);
void       gtk_label_set_markup(GtkLabel *label, const char *str);
void       gtk_label_set_justify(GtkLabel *label, GtkJustification jtype);
void       gtk_label_set_line_wrap(GtkLabel *label, gboolean wrap);
void       gtk_label_set_selectable(GtkLabel *label, gboolean setting);
void       gtk_label_set_xalign(GtkLabel *label, float xalign);
void       gtk_label_set_yalign(GtkLabel *label, float yalign);

GType      gtk_button_get_type(void);
GtkWidget *gtk_button_new(void);
GtkWidget *gtk_button_new_with_label(const char *label);
GtkWidget *gtk_button_new_with_mnemonic(const char *label);
void       gtk_button_set_label(GtkButton *button, const char *label);
const char *gtk_button_get_label(GtkButton *button);
void       gtk_button_set_relief(GtkButton *button, int relief);

GType      gtk_entry_get_type(void);
GtkWidget *gtk_entry_new(void);
void       gtk_entry_set_text(GtkEntry *entry, const char *text);
const char *gtk_entry_get_text(GtkEntry *entry);
void       gtk_entry_set_placeholder_text(GtkEntry *entry, const char *text);
void       gtk_entry_set_visibility(GtkEntry *entry, gboolean visible);
void       gtk_entry_set_max_length(GtkEntry *entry, int max);

GType           gtk_application_get_type(void);
GtkApplication *gtk_application_new(const char *application_id, int flags);
int             g_application_run(GtkApplication *app, int argc, char **argv);
void            g_application_quit(GtkApplication *app);
GtkWidget      *gtk_application_window_new(GtkApplication *app);

GtkSettings *gtk_settings_get_default(void);
GtkSettings *gtk_settings_get_for_screen(GdkScreen *screen);

GtkCssProvider *gtk_css_provider_new(void);
gboolean        gtk_css_provider_load_from_data(GtkCssProvider *provider, const char *data, long length, void **error);
void            gtk_style_context_add_provider_for_screen(GdkScreen *screen, gpointer provider, guint priority);
void            gtk_style_context_add_class(GtkStyleContext *context, const char *class_name);
void            gtk_style_context_remove_class(GtkStyleContext *context, const char *class_name);

#define g_signal_connect(instance, signal, handler, data) \
    g_signal_connect_data((instance), (signal), (GCallback)(handler), (data), 0, 0)
unsigned long g_signal_connect_data(gpointer instance, const char *signal, GCallback handler, gpointer data, GDestroyNotify destroy_data, int flags);

GType      gtk_drawing_area_get_type(void);
GtkWidget *gtk_drawing_area_new(void);

GType      gtk_grid_get_type(void);
GtkWidget *gtk_grid_new(void);
void       gtk_grid_attach(void *grid, GtkWidget *child, int left, int top, int width, int height);
void       gtk_grid_set_row_spacing(void *grid, guint spacing);
void       gtk_grid_set_column_spacing(void *grid, guint spacing);

GType      gtk_scrolled_window_get_type(void);
GtkWidget *gtk_scrolled_window_new(void *hadjustment, void *vadjustment);
void       gtk_scrolled_window_set_policy(void *sw, int hpolicy, int vpolicy);

GtkWidget *gtk_message_dialog_new(GtkWindow *parent, int flags, int type, int buttons, const char *format, ...);
int        gtk_dialog_run(void *dialog);
