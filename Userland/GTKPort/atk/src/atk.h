#pragma once

#include <stddef.h>
#include <stdint.h>

typedef unsigned long GType;
typedef int gboolean;
typedef unsigned int guint;
typedef void *gpointer;

typedef struct _AtkObject       AtkObject;
typedef struct _AtkObjectClass  AtkObjectClass;
typedef struct _AtkStateSet     AtkStateSet;
typedef struct _AtkRelationSet  AtkRelationSet;

typedef enum { ATK_ROLE_INVALID=0, ATK_ROLE_WINDOW=68, ATK_ROLE_APPLICATION=75, ATK_ROLE_FILLER=35,
               ATK_ROLE_LABEL=28, ATK_ROLE_PANEL=38, ATK_ROLE_PUSH_BUTTON=42 } AtkRole;
typedef enum { ATK_STATE_INVALID=0, ATK_STATE_ACTIVE, ATK_STATE_ENABLED=5, ATK_STATE_FOCUSABLE=7,
               ATK_STATE_FOCUSED=8, ATK_STATE_SHOWING=14, ATK_STATE_VISIBLE=17 } AtkStateType;
typedef enum { ATK_LAYER_INVALID=0, ATK_LAYER_BACKGROUND, ATK_LAYER_CANVAS, ATK_LAYER_WIDGET,
               ATK_LAYER_MDI, ATK_LAYER_POPUP, ATK_LAYER_OVERLAY, ATK_LAYER_WINDOW } AtkLayer;

struct _AtkObject {
    GType g_type;
    const char *name;
    const char *description;
    AtkObject *parent;
    AtkRole role;
    AtkRelationSet *relation_set;
    guint ref_count;
};

GType atk_object_get_type(void);
const char *atk_object_get_name(AtkObject *accessible);
void        atk_object_set_name(AtkObject *accessible, const char *name);
const char *atk_object_get_description(AtkObject *accessible);
void        atk_object_set_description(AtkObject *accessible, const char *description);
AtkObject  *atk_object_get_parent(AtkObject *accessible);
void        atk_object_set_parent(AtkObject *accessible, AtkObject *parent);
AtkRole     atk_object_get_role(AtkObject *accessible);
void        atk_object_set_role(AtkObject *accessible, AtkRole role);
int         atk_object_get_n_accessible_children(AtkObject *accessible);
AtkObject  *atk_object_ref_accessible_child(AtkObject *accessible, int i);
AtkStateSet *atk_object_ref_state_set(AtkObject *accessible);
AtkRelationSet *atk_object_ref_relation_set(AtkObject *accessible);
AtkLayer    atk_object_get_layer(AtkObject *accessible);
void        atk_object_notify_state_change(AtkObject *accessible, AtkStateType state, gboolean value);

AtkStateSet *atk_state_set_new(void);
gboolean     atk_state_set_add_state(AtkStateSet *set, AtkStateType type);
gboolean     atk_state_set_contains_state(AtkStateSet *set, AtkStateType type);
gboolean     atk_state_set_remove_state(AtkStateSet *set, AtkStateType type);

AtkRelationSet *atk_relation_set_new(void);

const char *atk_role_get_name(AtkRole role);
AtkRole     atk_role_for_name(const char *name);

AtkObject *atk_get_root(void);
const char *atk_get_toolkit_name(void);
const char *atk_get_toolkit_version(void);

typedef struct _AtkNoOpObject AtkNoOpObject;
GType       atk_no_op_object_get_type(void);
AtkObject  *atk_no_op_object_new(gpointer obj);

GType atk_component_get_type(void);
GType atk_action_get_type(void);
GType atk_text_get_type(void);
GType atk_value_get_type(void);
GType atk_selection_get_type(void);
GType atk_image_get_type(void);
GType atk_table_get_type(void);
