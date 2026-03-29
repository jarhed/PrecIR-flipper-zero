#include "precir_app.h"

/* ---- Display Size ---- */

static const char* const display_size_names[] = {
    "Medium (208x112)",
    "Large (296x128)",
};

static void precir_dm_config_size_change(VariableItem* item) {
    PrecIRApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->display_size = (PrecIRDisplaySize)index;
    variable_item_set_current_value_text(item, display_size_names[index]);
}

/* ---- Color Mode ---- */

static const char* const color_mode_names[] = {
    "B&W",
    "B/W/Red",
    "4-Color",
};

static void precir_dm_config_color_change(VariableItem* item) {
    PrecIRApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->color_mode = (PrecIRColorMode)index;
    variable_item_set_current_value_text(item, color_mode_names[index]);
}

/* ---- Protocol ---- */

static const char* const protocol_mode_names[] = {
    "PP4",
    "PP16 (fast)",
};

static void precir_dm_config_protocol_change(VariableItem* item) {
    PrecIRApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->protocol_mode = (PrecIRProtocolMode)index;
    variable_item_set_current_value_text(item, protocol_mode_names[index]);
}

/* ---- Enter callback (OK pressed) ---- */

static void precir_dm_config_enter_callback(void* context, uint32_t index) {
    UNUSED(index);
    PrecIRApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, PrecIREventConfigDone);
}

/* ---- Scene handlers ---- */

void precir_scene_dm_config_on_enter(void* context) {
    PrecIRApp* app = context;
    VariableItem* item;

    variable_item_list_reset(app->variable_item_list);

    /* Display Size */
    item = variable_item_list_add(
        app->variable_item_list, "Display Size", 2, precir_dm_config_size_change, app);
    variable_item_set_current_value_index(item, (uint8_t)app->display_size);
    variable_item_set_current_value_text(item, display_size_names[app->display_size]);

    /* Color Mode */
    item = variable_item_list_add(
        app->variable_item_list, "Color Mode", 3, precir_dm_config_color_change, app);
    variable_item_set_current_value_index(item, (uint8_t)app->color_mode);
    variable_item_set_current_value_text(item, color_mode_names[app->color_mode]);

    /* Protocol */
    item = variable_item_list_add(
        app->variable_item_list, "Protocol", 2, precir_dm_config_protocol_change, app);
    variable_item_set_current_value_index(item, (uint8_t)app->protocol_mode);
    variable_item_set_current_value_text(item, protocol_mode_names[app->protocol_mode]);

    variable_item_list_set_enter_callback(
        app->variable_item_list, precir_dm_config_enter_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewVariableItemList);
}

bool precir_scene_dm_config_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PrecIREventConfigDone) {
            scene_manager_next_scene(app->scene_manager, PrecIRSceneImageSelect);
            consumed = true;
        }
    }

    return consumed;
}

void precir_scene_dm_config_on_exit(void* context) {
    PrecIRApp* app = context;
    variable_item_list_reset(app->variable_item_list);
}
