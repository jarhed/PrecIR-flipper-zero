#include "precir_app.h"

static void precir_plid_input_callback(void* context) {
    PrecIRApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, PrecIREventPLIDEntered);
}

void precir_scene_plid_input_on_enter(void* context) {
    PrecIRApp* app = context;

    /* Clear any previous input */
    app->text_store[0] = '\0';
    app->barcode[0] = '\0';

    text_input_set_header_text(app->text_input, "Enter Barcode (17 digits)");
    text_input_set_result_callback(
        app->text_input, precir_plid_input_callback, app, app->barcode, PRECIR_BARCODE_MAX_LEN);

    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewTextInput);
}

bool precir_scene_plid_input_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PrecIREventPLIDEntered) {
            app->plid_valid = precir_plid_from_barcode(app->barcode, app->plid);

            if(app->display_type == PrecIRDisplayTypeDM) {
                scene_manager_next_scene(app->scene_manager, PrecIRSceneDMConfig);
            } else {
                scene_manager_next_scene(app->scene_manager, PrecIRSceneSegmentConfig);
            }
            consumed = true;
        }
    }

    return consumed;
}

void precir_scene_plid_input_on_exit(void* context) {
    PrecIRApp* app = context;
    text_input_reset(app->text_input);
}
