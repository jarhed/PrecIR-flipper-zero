#include "precir_app.h"

static const char* const precir_size_names[] = {
    "Medium 208x112",
    "Large 296x128",
};

static const char* const precir_color_names[] = {
    "B&W (1 plane)",
    "B/W/Red (2)",
    "4-Color (2)",
};

static const char* const precir_protocol_names[] = {
    "PP4",
    "PP16",
};

static const char* const precir_page_names[] = {
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "10",
    "11",
    "12",
    "13",
    "14",
    "15",
};

static void precir_config_size_change(VariableItem* item) {
    PrecIRApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->display_size = (PrecIRDisplaySize)index;
    variable_item_set_current_value_text(item, precir_size_names[index]);
}

static void precir_config_color_change(VariableItem* item) {
    PrecIRApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->color_mode = (PrecIRColorMode)index;
    variable_item_set_current_value_text(item, precir_color_names[index]);
}

static void precir_config_protocol_change(VariableItem* item) {
    PrecIRApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->protocol_mode = (PrecIRProtocolMode)index;
    variable_item_set_current_value_text(item, precir_protocol_names[index]);
}

static void precir_config_page_change(VariableItem* item) {
    PrecIRApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->display_page = index;
    variable_item_set_current_value_text(item, precir_page_names[index]);
}

static void precir_config_enter(void* context, uint32_t index) {
    UNUSED(index);
    PrecIRApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, PrecIREventConfigDone);
}

void precir_scene_dm_config_on_enter(void* context) {
    PrecIRApp* app = context;
    VariableItem* item;

    variable_item_list_reset(app->variable_item_list);

    item =
        variable_item_list_add(app->variable_item_list, "Size", 2, precir_config_size_change, app);
    variable_item_set_current_value_index(item, app->display_size);
    variable_item_set_current_value_text(item, precir_size_names[app->display_size]);

    item = variable_item_list_add(
        app->variable_item_list, "Planes", 3, precir_config_color_change, app);
    variable_item_set_current_value_index(item, app->color_mode);
    variable_item_set_current_value_text(item, precir_color_names[app->color_mode]);

    item = variable_item_list_add(
        app->variable_item_list, "Protocol", 2, precir_config_protocol_change, app);
    variable_item_set_current_value_index(item, app->protocol_mode);
    variable_item_set_current_value_text(item, precir_protocol_names[app->protocol_mode]);

    item = variable_item_list_add(
        app->variable_item_list, "Page", 16, precir_config_page_change, app);
    variable_item_set_current_value_index(item, app->display_page);
    variable_item_set_current_value_text(item, precir_page_names[app->display_page]);

    variable_item_list_set_enter_callback(app->variable_item_list, precir_config_enter, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewVariableItemList);
}

bool precir_scene_dm_config_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != PrecIREventConfigDone) {
        return false;
    }

    if(!precir_app_commit_active_settings(app)) {
        precir_app_show_message(app, "Save failed", "Settings were not saved.", "OK");
        return true;
    }

    notification_message(app->notification, &sequence_success);
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void precir_scene_dm_config_on_exit(void* context) {
    PrecIRApp* app = context;
    variable_item_list_reset(app->variable_item_list);
}
