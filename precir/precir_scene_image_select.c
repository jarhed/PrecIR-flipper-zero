#include "precir_app.h"

void precir_scene_image_select_on_enter(void* context) {
    PrecIRApp* app = context;
    PrecIRProfile* profile = precir_app_active_profile(app);
    if(!profile) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    /* Load the old path before opening the browser. Do not reload the profile
     * after selection: that was the v2.0 bug that replaced the chosen file
     * with the old path (often /ext). */
    precir_app_load_active_profile(app);
    profile = precir_app_active_profile(app);
    if(!profile) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, ".bmp", NULL);
    browser_options.base_path = STORAGE_EXT_PATH_PREFIX;
    browser_options.hide_ext = false;

    if(profile->bmp_path[0] != '\0') {
        furi_string_set_str(app->image_path, profile->bmp_path);
    } else {
        furi_string_set_str(app->image_path, STORAGE_EXT_PATH_PREFIX);
    }

    const bool selected =
        dialog_file_browser_show(app->dialogs, app->image_path, app->image_path, &browser_options);
    if(!selected) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    if(furi_string_size(app->image_path) >= PRECIR_PROFILE_PATH_SIZE) {
        precir_app_show_message(
            app, "Path too long", "Choose a BMP in a\nshorter folder path.", "OK");
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    /* Keep a stable copy because precir_app_load_image() updates image_path
     * after a successful decode. */
    char selected_path[PRECIR_PROFILE_PATH_SIZE];
    strlcpy(selected_path, furi_string_get_cstr(app->image_path), sizeof(selected_path));
    if(!precir_app_load_image(app, selected_path)) {
        precir_app_show_message(
            app, "BMP decode failed", "The file could not be\nread as pixel data.", "OK");
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    char old_path[PRECIR_PROFILE_PATH_SIZE];
    strlcpy(old_path, profile->bmp_path, sizeof(old_path));
    strlcpy(profile->bmp_path, selected_path, sizeof(profile->bmp_path));

    if(!precir_profiles_save(app->storage, &app->profiles)) {
        strlcpy(profile->bmp_path, old_path, sizeof(profile->bmp_path));
        precir_app_show_message(app, "Save failed", "BMP path was not saved.", "OK");
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    app->display_type = PrecIRDisplayTypeDM;
    app->transmit_kind = PrecIRTransmitKindImage;
    scene_manager_next_scene(app->scene_manager, PrecIRSceneTransmit);
}

bool precir_scene_image_select_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void precir_scene_image_select_on_exit(void* context) {
    UNUSED(context);
}
