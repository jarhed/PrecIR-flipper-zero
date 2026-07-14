#include "precir_app.h"

enum {
    PrecIRMainSelectionProfileList,
    PrecIRMainSelectionNewTag,
    PrecIRMainSelectionTestClear,
    PrecIRMainSelectionAbout,
};

static void precir_main_menu_callback(void* context, uint32_t index) {
    PrecIRApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void precir_scene_main_menu_on_enter(void* context) {
    PrecIRApp* app = context;

    submenu_reset(app->submenu);
    app->active_profile = -1;

    snprintf(
        app->text_store,
        sizeof(app->text_store),
        "Saved Tags (%u)",
        (unsigned int)app->profiles.count);

    submenu_add_item(
        app->submenu, app->text_store, PrecIREventProfileList, precir_main_menu_callback, app);
    submenu_add_item(app->submenu, "+ New Tag", PrecIREventNewTag, precir_main_menu_callback, app);
    submenu_add_item(
        app->submenu, "Test Clear", PrecIREventTestClear, precir_main_menu_callback, app);
    submenu_add_item(app->submenu, "About", PrecIREventAbout, precir_main_menu_callback, app);

    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, PrecIRSceneMainMenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewSubmenu);
}

bool precir_scene_main_menu_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case PrecIREventProfileList:
        scene_manager_set_scene_state(
            app->scene_manager, PrecIRSceneMainMenu, PrecIRMainSelectionProfileList);
        app->profile_list_calibration = false;
        scene_manager_next_scene(app->scene_manager, PrecIRSceneProfileList);
        return true;

    case PrecIREventNewTag:
        scene_manager_set_scene_state(
            app->scene_manager, PrecIRSceneMainMenu, PrecIRMainSelectionNewTag);
        if(app->profiles.count >= PRECIR_PROFILE_MAX_COUNT) {
            precir_app_show_message(
                app, "Profiles full", "Delete one of the 8\nsaved tags first.", "OK");
            return true;
        }
        app->pending_auto_calibrate = false;
        scene_manager_next_scene(app->scene_manager, PrecIRScenePLIDInput);
        return true;

    case PrecIREventTestClear:
        scene_manager_set_scene_state(
            app->scene_manager, PrecIRSceneMainMenu, PrecIRMainSelectionTestClear);
        if(app->profiles.count == 0U) {
            app->pending_auto_calibrate = true;
            scene_manager_next_scene(app->scene_manager, PrecIRScenePLIDInput);
        } else {
            app->profile_list_calibration = true;
            scene_manager_next_scene(app->scene_manager, PrecIRSceneProfileList);
        }
        return true;

    case PrecIREventAbout:
        scene_manager_set_scene_state(
            app->scene_manager, PrecIRSceneMainMenu, PrecIRMainSelectionAbout);
        scene_manager_next_scene(app->scene_manager, PrecIRSceneAbout);
        return true;

    default:
        return false;
    }
}

void precir_scene_main_menu_on_exit(void* context) {
    PrecIRApp* app = context;
    submenu_reset(app->submenu);
}
