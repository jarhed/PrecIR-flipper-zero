#include "precir_app.h"

#define PRECIR_PROFILE_LIST_NEW_TAG 50U

static void precir_profile_list_callback(void* context, uint32_t index) {
    PrecIRApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void precir_scene_profile_list_on_enter(void* context) {
    PrecIRApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(
        app->submenu, app->profile_list_calibration ? "Test which tag?" : "Saved tags");

    for(uint8_t i = 0; i < app->profiles.count; ++i) {
        submenu_add_item(
            app->submenu, app->profiles.profiles[i].barcode, i, precir_profile_list_callback, app);
    }

    if(app->profiles.count < PRECIR_PROFILE_MAX_COUNT) {
        submenu_add_item(
            app->submenu,
            "+ New Tag",
            PRECIR_PROFILE_LIST_NEW_TAG,
            precir_profile_list_callback,
            app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewSubmenu);
}

bool precir_scene_profile_list_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == PRECIR_PROFILE_LIST_NEW_TAG) {
        app->pending_auto_calibrate = app->profile_list_calibration;
        scene_manager_next_scene(app->scene_manager, PrecIRScenePLIDInput);
        return true;
    }

    if(event.event >= app->profiles.count) return false;

    app->active_profile = (int8_t)event.event;
    precir_app_load_active_profile(app);
    app->auto_calibrate = app->profile_list_calibration;
    scene_manager_next_scene(app->scene_manager, PrecIRSceneProfileActions);
    return true;
}

void precir_scene_profile_list_on_exit(void* context) {
    PrecIRApp* app = context;
    submenu_reset(app->submenu);
}
