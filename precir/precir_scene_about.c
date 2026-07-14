#include "precir_app.h"

void precir_scene_about_on_enter(void* context) {
    PrecIRApp* app = context;

    widget_reset(app->widget);
    widget_add_text_box_element(
        app->widget,
        4,
        2,
        120,
        60,
        AlignCenter,
        AlignTop,
        "\e#PrecIR Profiles 2.1.1\e#\n"
        "Pricer ESL IR sender\n"
        "Saved barcode + BMP profiles\n"
        "White-screen calibration\n"
        "1.25 MHz | PP4 / PP16",
        false);

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
