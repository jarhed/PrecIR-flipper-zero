#include "precir_app.h"

static void precir_plid_input_callback(void* context) {
    PrecIRApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, PrecIREventPLIDEntered);
}

static bool precir_plid_input_validator(const char* text, FuriString* error, void* context) {
    UNUSED(context);

    switch(precir_barcode_validate(text)) {
    case PrecIRBarcodeValid:
        return true;
    case PrecIRBarcodeInvalidLength:
        furi_string_set(error, "Enter exactly 17 characters");
        break;
    case PrecIRBarcodeInvalidFormat:
        furi_string_set(error, "Use A-Z, then digits only");
        break;
    case PrecIRBarcodeInvalidFamily:
        furi_string_set(error, "Second character must be 4");
        break;
    case PrecIRBarcodeAddressOutOfRange:
        furi_string_set(error, "Address fields exceed 65535");
        break;
    case PrecIRBarcodeInvalidChecksum:
        furi_string_set(error, "Last-digit checksum is wrong");
        break;
    default:
        furi_string_set(error, "Invalid barcode");
        break;
    }

    return false;
}

static void
    precir_plid_input_open_actions(PrecIRApp* app, int8_t profile_index, bool auto_calibrate) {
    /* Remove the entry scene and any stale path between it and its owner. */
    if(!scene_manager_search_and_switch_to_previous_scene(
           app->scene_manager, PrecIRSceneProfileList)) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, PrecIRSceneMainMenu);
    }

    /* MainMenu clears the active selection when it is re-entered, so publish
     * the new selection only after pruning the input scene. */
    app->active_profile = profile_index;
    app->auto_calibrate = auto_calibrate;
    precir_app_load_active_profile(app);
    scene_manager_next_scene(app->scene_manager, PrecIRSceneProfileActions);
}

void precir_scene_plid_input_on_enter(void* context) {
    PrecIRApp* app = context;

    memset(app->barcode, 0, sizeof(app->barcode));
    app->plid_valid = false;

    text_input_set_header_text(app->text_input, "Pricer barcode (17 chars)");
    text_input_set_minimum_length(app->text_input, 17);
    text_input_set_validator(app->text_input, precir_plid_input_validator, app);
    text_input_set_result_callback(
        app->text_input,
        precir_plid_input_callback,
        app,
        app->barcode,
        PRECIR_PROFILE_BARCODE_SIZE,
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewTextInput);
}

bool precir_scene_plid_input_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != PrecIREventPLIDEntered) {
        return false;
    }

    if(precir_barcode_validate(app->barcode) != PrecIRBarcodeValid) {
        precir_app_show_message(
            app, "Invalid barcode", "Need 17 characters,\nincluding checksum.", "Retry");
        return true;
    }

    int32_t profile_index = precir_profiles_find(&app->profiles, app->barcode);
    if(profile_index < 0) {
        uint8_t new_index = 0;
        if(!precir_profiles_add(&app->profiles, app->barcode, &new_index)) {
            precir_app_show_message(app, "Save failed", "Could not save this tag.", "OK");
            return true;
        }

        if(!precir_profiles_save(app->storage, &app->profiles)) {
            precir_profiles_delete(&app->profiles, new_index);
            precir_app_show_message(app, "Save failed", "Could not save this tag.", "OK");
            return true;
        }
        profile_index = new_index;
    }

    precir_plid_input_open_actions(app, (int8_t)profile_index, app->pending_auto_calibrate);
    return true;
}

void precir_scene_plid_input_on_exit(void* context) {
    PrecIRApp* app = context;
    text_input_reset(app->text_input);
}
