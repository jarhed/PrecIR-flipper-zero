#include "precir_app.h"

static void precir_profile_actions_callback(void* context, uint32_t index) {
    PrecIRApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void precir_profile_actions_return_to_main(PrecIRApp* app) {
    if(!scene_manager_search_and_switch_to_previous_scene(
           app->scene_manager, PrecIRSceneMainMenu)) {
        view_dispatcher_stop(app->view_dispatcher);
    }
}

static bool precir_profile_actions_delete(PrecIRApp* app) {
    DialogMessage* message = dialog_message_alloc();
    if(!message) {
        precir_app_show_message(app, "Delete failed", "Profile was not removed.", "OK");
        return false;
    }

    dialog_message_set_header(message, "Delete saved tag?", 64, 6, AlignCenter, AlignCenter);
    dialog_message_set_text(
        message,
        "Barcode, settings, and\nBMP path will be removed.",
        64,
        30,
        AlignCenter,
        AlignCenter);
    dialog_message_set_buttons(message, "Delete", NULL, "Cancel");

    const DialogMessageButton result = dialog_message_show(app->dialogs, message);
    dialog_message_free(message);

    /* v2.1 accidentally checked Right/Cancel here. Delete only on the
     * explicitly labelled left button. */
    if(result != DialogMessageButtonLeft) return false;

    PrecIRProfileStore backup = app->profiles;
    if(!precir_profiles_delete(&app->profiles, (uint8_t)app->active_profile) ||
       !precir_profiles_save(app->storage, &app->profiles)) {
        app->profiles = backup;
        precir_app_show_message(app, "Delete failed", "Profile was not removed.", "OK");
        return false;
    }

    app->active_profile = -1;
    precir_profile_actions_return_to_main(app);
    return true;
}

void precir_scene_profile_actions_on_enter(void* context) {
    PrecIRApp* app = context;
    PrecIRProfile* profile = precir_app_active_profile(app);
    if(!profile) {
        precir_profile_actions_return_to_main(app);
        return;
    }

    if(app->auto_calibrate) {
        app->auto_calibrate = false;
        app->calibration_index = 0;
        app->calibration_state = PrecIRCalibrationStateIdle;
        scene_manager_next_scene(app->scene_manager, PrecIRSceneCalibrate);
        return;
    }

    precir_app_load_active_profile(app);
    profile = precir_app_active_profile(app);
    if(!profile) {
        precir_profile_actions_return_to_main(app);
        return;
    }

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, profile->barcode);

    if(profile->bmp_path[0] != '\0') {
        submenu_add_item(
            app->submenu,
            "Send Saved Image",
            PrecIREventSendSavedImage,
            precir_profile_actions_callback,
            app);
    }

    submenu_add_item(
        app->submenu,
        profile->bmp_path[0] ? "Change BMP" : "Choose BMP",
        PrecIREventChooseBMP,
        precir_profile_actions_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Clear With Saved",
        PrecIREventClearSaved,
        precir_profile_actions_callback,
        app);
    submenu_add_item(
        app->submenu, "Test Clear", PrecIREventTestClear, precir_profile_actions_callback, app);
    submenu_add_item(
        app->submenu,
        "Edit Settings",
        PrecIREventEditSettings,
        precir_profile_actions_callback,
        app);
    submenu_add_item(
        app->submenu, "Set Segments", PrecIREventSetSegments, precir_profile_actions_callback, app);
    submenu_add_item(
        app->submenu, "Delete Tag", PrecIREventDeleteTag, precir_profile_actions_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewSubmenu);
}

bool precir_scene_profile_actions_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    PrecIRProfile* profile = precir_app_active_profile(app);
    if(!profile) return false;

    switch(event.event) {
    case PrecIREventSendSavedImage:
        precir_app_load_active_profile(app);
        profile = precir_app_active_profile(app);
        if(!profile || profile->bmp_path[0] == '\0' ||
           !precir_app_load_image(app, profile->bmp_path)) {
            precir_app_show_message(
                app, "Image unavailable", "BMP was moved, deleted,\nor is not supported.", "OK");
            return true;
        }
        app->display_type = PrecIRDisplayTypeDM;
        app->transmit_kind = PrecIRTransmitKindImage;
        scene_manager_next_scene(app->scene_manager, PrecIRSceneTransmit);
        return true;

    case PrecIREventChooseBMP:
        scene_manager_next_scene(app->scene_manager, PrecIRSceneImageSelect);
        return true;

    case PrecIREventClearSaved:
        precir_app_load_active_profile(app);
        if(!precir_app_make_white_image(app)) {
            precir_app_show_message(app, "Out of memory", "Could not build white image.", "OK");
            return true;
        }
        app->display_type = PrecIRDisplayTypeDM;
        app->transmit_kind = PrecIRTransmitKindClearSaved;
        scene_manager_next_scene(app->scene_manager, PrecIRSceneTransmit);
        return true;

    case PrecIREventTestClear:
        app->calibration_index = 0;
        app->calibration_state = PrecIRCalibrationStateIdle;
        scene_manager_next_scene(app->scene_manager, PrecIRSceneCalibrate);
        return true;

    case PrecIREventEditSettings:
        precir_app_load_active_profile(app);
        scene_manager_next_scene(app->scene_manager, PrecIRSceneDMConfig);
        return true;

    case PrecIREventSetSegments:
        precir_app_load_active_profile(app);
        app->display_type = PrecIRDisplayTypeSegment;
        scene_manager_next_scene(app->scene_manager, PrecIRSceneSegmentConfig);
        return true;

    case PrecIREventDeleteTag:
        precir_profile_actions_delete(app);
        return true;

    default:
        return false;
    }
}

void precir_scene_profile_actions_on_exit(void* context) {
    PrecIRApp* app = context;
    submenu_reset(app->submenu);
}
