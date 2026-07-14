#include "precir_app.h"

#define PRECIR_CALIBRATION_WAIT_MS 5000U

typedef struct {
    PrecIRDisplaySize size;
    PrecIRColorMode color;
    PrecIRProtocolMode protocol;
    uint8_t page;
} PrecIRCalibrationCandidate;

/* Keep the likely 296x128 two-plane profiles first. The displayed selection
 * number is this zero-based array index plus one. */
static const PrecIRCalibrationCandidate precir_calibration_candidates[] = {
    {PrecIRDisplaySizeLarge, PrecIRColorModeBWR, PrecIRProtocolPP4, 0},
    {PrecIRDisplaySizeLarge, PrecIRColorModeBWR, PrecIRProtocolPP16, 0},
    {PrecIRDisplaySizeLarge, PrecIRColorModeBWR, PrecIRProtocolPP4, 1},
    {PrecIRDisplaySizeLarge, PrecIRColorModeBWR, PrecIRProtocolPP16, 1},
    {PrecIRDisplaySizeLarge, PrecIRColorModeBW, PrecIRProtocolPP4, 0},
    {PrecIRDisplaySizeLarge, PrecIRColorModeBW, PrecIRProtocolPP16, 0},
    {PrecIRDisplaySizeLarge, PrecIRColorModeBW, PrecIRProtocolPP4, 1},
    {PrecIRDisplaySizeLarge, PrecIRColorModeBW, PrecIRProtocolPP16, 1},
    {PrecIRDisplaySizeMedium, PrecIRColorModeBWR, PrecIRProtocolPP4, 0},
    {PrecIRDisplaySizeMedium, PrecIRColorModeBWR, PrecIRProtocolPP16, 0},
    {PrecIRDisplaySizeMedium, PrecIRColorModeBWR, PrecIRProtocolPP4, 1},
    {PrecIRDisplaySizeMedium, PrecIRColorModeBWR, PrecIRProtocolPP16, 1},
    {PrecIRDisplaySizeMedium, PrecIRColorModeBW, PrecIRProtocolPP4, 0},
    {PrecIRDisplaySizeMedium, PrecIRColorModeBW, PrecIRProtocolPP16, 0},
    {PrecIRDisplaySizeMedium, PrecIRColorModeBW, PrecIRProtocolPP4, 1},
    {PrecIRDisplaySizeMedium, PrecIRColorModeBW, PrecIRProtocolPP16, 1},
};

static size_t precir_calibration_count(void) {
    return sizeof(precir_calibration_candidates) / sizeof(precir_calibration_candidates[0]);
}

static void precir_calibration_apply(PrecIRApp* app) {
    const PrecIRCalibrationCandidate* candidate =
        &precir_calibration_candidates[app->calibration_index];
    app->display_type = PrecIRDisplayTypeDM;
    app->display_size = candidate->size;
    app->color_mode = candidate->color;
    app->protocol_mode = candidate->protocol;
    app->display_page = candidate->page;
}

static bool precir_calibration_start(PrecIRApp* app) {
    precir_calibration_apply(app);
    if(!precir_app_make_white_image(app)) {
        precir_app_show_message(app, "Out of memory", "Could not build white image.", "OK");
        return false;
    }

    app->transmit_kind = PrecIRTransmitKindTestClear;
    app->calibration_state = PrecIRCalibrationStateSending;
    scene_manager_next_scene(app->scene_manager, PrecIRSceneTransmit);
    return true;
}

static void precir_calibration_button(GuiButtonType button, InputType input_type, void* context) {
    if(input_type != InputTypeShort) return;

    PrecIRApp* app = context;
    if(button == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, PrecIREventCalibrationSave);
    } else if(button == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, PrecIREventCalibrationNext);
    }
}

static uint32_t precir_calibration_seconds_remaining(const PrecIRApp* app) {
    const uint32_t duration = furi_ms_to_ticks(PRECIR_CALIBRATION_WAIT_MS);
    const uint32_t elapsed = furi_get_tick() - app->calibration_start_tick;
    if(elapsed >= duration) return 0;

    const uint32_t frequency = furi_kernel_get_tick_frequency();
    if(frequency == 0U) return 0;

    const uint32_t remaining_ticks = duration - elapsed;
    const uint32_t remaining_ms = (remaining_ticks * 1000U + frequency - 1U) / frequency;
    return (remaining_ms + 999U) / 1000U;
}

static void precir_calibration_render(PrecIRApp* app) {
    char configuration[96];
    const char* size = app->display_size == PrecIRDisplaySizeLarge ? "296x128" : "208x112";
    const char* planes = app->color_mode == PrecIRColorModeBW ? "1 plane" : "2 planes";
    const char* protocol = app->protocol_mode == PrecIRProtocolPP4 ? "PP4" : "PP16";

    snprintf(
        configuration,
        sizeof(configuration),
        "%u/%u  %s  %s\n%s  Page %u",
        (unsigned int)app->calibration_index + 1U,
        (unsigned int)precir_calibration_count(),
        size,
        planes,
        protocol,
        (unsigned int)app->display_page);

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Is tag fully white?");
    widget_add_text_box_element(
        app->widget, 4, 18, 120, 31, AlignCenter, AlignTop, configuration, false);

    if(app->calibration_state == PrecIRCalibrationStateWaiting) {
        char wait_text[32];
        snprintf(
            wait_text,
            sizeof(wait_text),
            "Wait %lus...",
            (unsigned long)precir_calibration_seconds_remaining(app));
        widget_add_string_element(
            app->widget, 64, 51, AlignCenter, AlignTop, FontSecondary, wait_text);
    } else if(app->calibration_state == PrecIRCalibrationStateReady) {
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, "Save", precir_calibration_button, app);
        widget_add_button_element(
            app->widget, GuiButtonTypeRight, "Next", precir_calibration_button, app);
    }
}

void precir_scene_calibrate_on_enter(void* context) {
    PrecIRApp* app = context;
    if(app->calibration_index >= precir_calibration_count()) app->calibration_index = 0;

    if(app->calibration_state == PrecIRCalibrationStateIdle) {
        if(!precir_calibration_start(app)) {
            scene_manager_previous_scene(app->scene_manager);
        }
        return;
    }

    if(app->calibration_state == PrecIRCalibrationStateError) {
        notification_message(app->notification, &sequence_error);
        precir_app_show_message(
            app, "Test failed", "IR could not send this\nwhite test. Try again.", "OK");
        app->calibration_state = PrecIRCalibrationStateIdle;
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    precir_calibration_apply(app);
    precir_calibration_render(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewWidget);
}

bool precir_scene_calibrate_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;

    if(event.type == SceneManagerEventTypeTick &&
       app->calibration_state == PrecIRCalibrationStateWaiting) {
        const uint32_t elapsed = furi_get_tick() - app->calibration_start_tick;
        if(elapsed >= furi_ms_to_ticks(PRECIR_CALIBRATION_WAIT_MS)) {
            app->calibration_state = PrecIRCalibrationStateReady;
            notification_message(app->notification, &sequence_single_vibro);
        }
        precir_calibration_render(app);
        return true;
    }

    if(event.type != SceneManagerEventTypeCustom ||
       app->calibration_state != PrecIRCalibrationStateReady) {
        return false;
    }

    if(event.event == PrecIREventCalibrationSave) {
        if(!precir_app_commit_active_settings(app)) {
            precir_app_show_message(app, "Save failed", "Settings were not saved.", "OK");
            return true;
        }

        notification_message(app->notification, &sequence_success);
        precir_app_show_message(
            app, "Settings saved", "This barcode will reuse\nthese working settings.", "OK");
        app->calibration_state = PrecIRCalibrationStateIdle;
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }

    if(event.event == PrecIREventCalibrationNext) {
        ++app->calibration_index;
        if(app->calibration_index >= precir_calibration_count()) {
            app->calibration_index = 0;
            precir_app_show_message(
                app, "All tests tried", "Starting again at the\nmost likely setting.", "OK");
        }

        app->calibration_state = PrecIRCalibrationStateIdle;
        if(!precir_calibration_start(app)) {
            scene_manager_previous_scene(app->scene_manager);
        }
        return true;
    }

    return false;
}

void precir_scene_calibrate_on_exit(void* context) {
    PrecIRApp* app = context;
    widget_reset(app->widget);
}
