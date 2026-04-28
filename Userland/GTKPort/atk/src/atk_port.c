#include "atk.h"
#include <string.h>

extern void *calloc(unsigned long, unsigned long);
extern void  free(void *);
extern char *strdup(const char *);

struct _AtkStateSet    { unsigned long states; guint ref_count; };
struct _AtkRelationSet { guint ref_count; };
struct _AtkNoOpObject  { AtkObject parent; };

static AtkObject g_atk_root = { .name = "ImplusOS", .role = ATK_ROLE_APPLICATION, .ref_count = 1 };

GType atk_object_get_type(void) { return 300; }

const char *atk_object_get_name(AtkObject *a) { return a ? a->name : NULL; }
void atk_object_set_name(AtkObject *a, const char *n) { if (a) a->name = n; }
const char *atk_object_get_description(AtkObject *a) { return a ? a->description : NULL; }
void atk_object_set_description(AtkObject *a, const char *d) { if (a) a->description = d; }
AtkObject *atk_object_get_parent(AtkObject *a) { return a ? a->parent : NULL; }
void atk_object_set_parent(AtkObject *a, AtkObject *p) { if (a) a->parent = p; }
AtkRole atk_object_get_role(AtkObject *a) { return a ? a->role : ATK_ROLE_INVALID; }
void atk_object_set_role(AtkObject *a, AtkRole r) { if (a) a->role = r; }
int atk_object_get_n_accessible_children(AtkObject *a) { (void)a; return 0; }
AtkObject *atk_object_ref_accessible_child(AtkObject *a, int i) { (void)a;(void)i; return NULL; }
AtkLayer atk_object_get_layer(AtkObject *a) { (void)a; return ATK_LAYER_WIDGET; }
void atk_object_notify_state_change(AtkObject *a, AtkStateType s, gboolean v) { (void)a;(void)s;(void)v; }

AtkStateSet *atk_object_ref_state_set(AtkObject *a) {
    (void)a;
    AtkStateSet *s = (AtkStateSet*)calloc(1, sizeof(*s));
    s->ref_count = 1;
    s->states = (1UL << ATK_STATE_ENABLED) | (1UL << ATK_STATE_VISIBLE) | (1UL << ATK_STATE_SHOWING);
    return s;
}

AtkRelationSet *atk_object_ref_relation_set(AtkObject *a) {
    (void)a;
    AtkRelationSet *r = (AtkRelationSet*)calloc(1, sizeof(*r));
    r->ref_count = 1;
    return r;
}

AtkStateSet *atk_state_set_new(void) { AtkStateSet *s = (AtkStateSet*)calloc(1, sizeof(*s)); s->ref_count = 1; return s; }
gboolean atk_state_set_add_state(AtkStateSet *s, AtkStateType t) { s->states |= (1UL << t); return 1; }
gboolean atk_state_set_contains_state(AtkStateSet *s, AtkStateType t) { return (s->states & (1UL << t)) != 0; }
gboolean atk_state_set_remove_state(AtkStateSet *s, AtkStateType t) { s->states &= ~(1UL << t); return 1; }

AtkRelationSet *atk_relation_set_new(void) { AtkRelationSet *r = (AtkRelationSet*)calloc(1, sizeof(*r)); r->ref_count = 1; return r; }

const char *atk_role_get_name(AtkRole r) {
    switch (r) {
        case ATK_ROLE_WINDOW: return "window";
        case ATK_ROLE_APPLICATION: return "application";
        case ATK_ROLE_LABEL: return "label";
        case ATK_ROLE_PUSH_BUTTON: return "push-button";
        case ATK_ROLE_PANEL: return "panel";
        case ATK_ROLE_FILLER: return "filler";
        default: return "unknown";
    }
}

AtkRole atk_role_for_name(const char *n) {
    if (!n) return ATK_ROLE_INVALID;
    if (strcmp(n, "window") == 0) return ATK_ROLE_WINDOW;
    if (strcmp(n, "application") == 0) return ATK_ROLE_APPLICATION;
    if (strcmp(n, "label") == 0) return ATK_ROLE_LABEL;
    return ATK_ROLE_INVALID;
}

AtkObject *atk_get_root(void) { return &g_atk_root; }
const char *atk_get_toolkit_name(void) { return "gtk"; }
const char *atk_get_toolkit_version(void) { return "3.24.0"; }

GType atk_no_op_object_get_type(void) { return 301; }
AtkObject *atk_no_op_object_new(gpointer obj) {
    (void)obj;
    AtkObject *a = (AtkObject*)calloc(1, sizeof(AtkObject));
    a->role = ATK_ROLE_FILLER; a->ref_count = 1;
    return a;
}

GType atk_component_get_type(void) { return 310; }
GType atk_action_get_type(void) { return 311; }
GType atk_text_get_type(void) { return 312; }
GType atk_value_get_type(void) { return 313; }
GType atk_selection_get_type(void) { return 314; }
GType atk_image_get_type(void) { return 315; }
GType atk_table_get_type(void) { return 316; }
