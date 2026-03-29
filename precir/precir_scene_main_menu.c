#include "precir_app.h"

/** Submenu callback -- forwards the selected index as a custom event. */
static void precir_scene_main_menu_submenu_callback(void* context, uint32_t index) {
    PrecIRApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

/** on_enter: populate submenu and switch to it. */
void precir_scene_main_menu_on_enter(void* context) {
    PrecIRApp* app = context;

    submenu_reset(app->submenu);

    submenu_add_item(
        app->submenu,
        "Send Image",
        PrecIREventSendImage,
        precir_scene_main_menu_submenu_callback,
        app);

    submenu_add_item(
        app->submenu,
        "Set Segments",
        PrecIREventSetSegments,
        precir_scene_main_menu_submenu_callback,
        app);

    submenu_add_item(
        app->submenu,
        "About",
        PrecIREventAbout,
        precir_scene_main_menu_submenu_callback,
        app);

    submenu_set_selected_item(
        app->submenu,
        scene_manager_get_scene_state(app->scene_manager, PrecIRSceneMainMenu));

    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewSubmenu);
}

/** on_event: handle custom events from the submenu. */
bool precir_scene_main_menu_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case PrecIREventSendImage:
            app->display_type = PrecIRDisplayTypeDM;
            scene_manager_set_scene_state(
                app->scene_manager, PrecIRSceneMainMenu, PrecIREventSendImage);
            scene_manager_next_scene(app->scene_manager, PrecIRScenePLIDInput);
            consumed = true;
            break;

        case PrecIREventSetSegments:
            app->display_type = PrecIRDisplayTypeSegment;
            scene_manager_set_scene_state(
                app->scene_manager, PrecIRSceneMainMenu, PrecIREventSetSegments);
            scene_manager_next_scene(app->scene_manager, PrecIRScenePLIDInput);
            consumed = true;
            break;

        case PrecIREventAbout:
            scene_manager_set_scene_state(
                app->scene_manager, PrecIRSceneMainMenu, PrecIREventAbout);
            scene_manager_next_scene(app->scene_manager, PrecIRSceneAbout);
            consumed = true;
            break;

        default:
            break;
        }
    }

    return consumed;
}

/** on_exit: clean up the submenu. */
void precir_scene_main_menu_on_exit(void* context) {
    PrecIRApp* app = context;
    submenu_reset(app->submenu);
}
