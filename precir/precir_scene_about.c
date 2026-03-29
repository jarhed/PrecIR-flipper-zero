#include "precir_app.h"

void precir_scene_about_on_enter(void* context) {
    PrecIRApp* app = context;

    widget_reset(app->widget);

    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "PrecIR for Flipper Zero");
    widget_add_string_element(
        app->widget, 64, 16, AlignCenter, AlignTop, FontSecondary, "ESL IR Communicator");
    widget_add_string_element(
        app->widget, 64, 32, AlignCenter, AlignTop, FontSecondary, "Based on furrtek's PrecIR");
    widget_add_string_element(
        app->widget, 64, 44, AlignCenter, AlignTop, FontSecondary, "github.com/furrtek/PrecIR");
    widget_add_string_element(
        app->widget, 64, 60, AlignCenter, AlignTop, FontSecondary, "Carrier: 1.263 MHz");
    widget_add_string_element(
        app->widget, 64, 72, AlignCenter, AlignTop, FontSecondary, "Protocols: PP4, PP16");

    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewWidget);
}

bool precir_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void precir_scene_about_on_exit(void* context) {
    PrecIRApp* app = context;
    widget_reset(app->widget);
}
