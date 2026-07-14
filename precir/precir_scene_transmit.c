#include "precir_app.h"

#define PRECIR_TRANSMIT_STACK_SIZE 2048U

static bool precir_transmit_lock(PrecIRApp* app) {
    return furi_mutex_acquire(app->tx_mutex, FuriWaitForever) == FuriStatusOk;
}

static void precir_transmit_progress(uint16_t current, uint16_t total, void* context) {
    PrecIRApp* app = context;
    if(precir_transmit_lock(app)) {
        app->tx_progress = current;
        app->tx_total = total;
        furi_mutex_release(app->tx_mutex);
    }
}

static int32_t precir_transmit_worker(void* context) {
    PrecIRApp* app = context;
    bool success = false;

    /* Direct 1.25 MHz carrier timing is sensitive to preemption. */
    furi_thread_set_current_priority(FuriThreadPriorityHighest);
    precir_ir_set_protocol(app->transmitter, app->protocol_mode);

    if(app->plid_valid) {
        if(app->transmit_kind == PrecIRTransmitKindSegments) {
            success = precir_ir_send_segment(app->transmitter, app->plid, app->segment_bitmap);
        } else if(app->image_data && app->image_data_len > 0U) {
            success = precir_ir_send_image(
                app->transmitter,
                app->plid,
                app->image_data,
                app->image_data_len,
                precir_display_width(app->display_size),
                precir_display_height(app->display_size),
                app->image_compression,
                app->display_page,
                precir_transmit_progress,
                app);
        }
    }

    if(precir_transmit_lock(app)) {
        app->tx_success = success && !app->tx_cancel_requested;
        app->transmitting = false;
        furi_mutex_release(app->tx_mutex);
    }

    view_dispatcher_send_custom_event(app->view_dispatcher, PrecIREventTransmitDone);
    return 0;
}

static const char* precir_transmit_title(PrecIRTransmitKind kind) {
    switch(kind) {
    case PrecIRTransmitKindClearSaved:
        return "Clear saved";
    case PrecIRTransmitKindTestClear:
        return "Test clear";
    case PrecIRTransmitKindSegments:
        return "Send segments";
    case PrecIRTransmitKindImage:
    default:
        return "Send BMP";
    }
}

static void precir_transmit_render(PrecIRApp* app, bool stopping) {
    bool transmitting = false;
    uint16_t current = 0U;
    uint16_t total = 0U;
    if(precir_transmit_lock(app)) {
        transmitting = app->transmitting;
        current = app->tx_progress;
        total = app->tx_total;
        furi_mutex_release(app->tx_mutex);
    }

    char config[32];
    snprintf(
        config,
        sizeof(config),
        "%ux%u %s %s P%u",
        precir_display_width(app->display_size),
        precir_display_height(app->display_size),
        app->color_mode == PrecIRColorModeBW ? "1P" : "2P",
        app->protocol_mode == PrecIRProtocolPP4 ? "PP4" : "PP16",
        app->display_page);

    char status[96];
    if(stopping) {
        strlcpy(status, "Stopping safely...\nFinish current IR frame", sizeof(status));
    } else if(!transmitting) {
        snprintf(status, sizeof(status), "Finishing...\n%s", config);
    } else if(total > 0U) {
        snprintf(
            status,
            sizeof(status),
            "Frame %u/%u\n%s\nKeep aimed at sensor",
            current,
            total,
            config);
    } else {
        snprintf(status, sizeof(status), "Waking tag...\n%s\nKeep aimed at sensor", config);
    }

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget,
        64,
        3,
        AlignCenter,
        AlignTop,
        FontPrimary,
        precir_transmit_title(app->transmit_kind));
    widget_add_text_box_element(
        app->widget, 4, 22, 120, 38, AlignCenter, AlignCenter, status, false);
}

static void precir_transmit_join_thread(PrecIRApp* app) {
    if(!app->tx_thread) return;

    furi_thread_join(app->tx_thread);
    furi_thread_free(app->tx_thread);
    app->tx_thread = NULL;
}

static void precir_transmit_start_failed(PrecIRApp* app, const char* header, const char* text) {
    if(app->transmit_kind == PrecIRTransmitKindTestClear) {
        app->calibration_state = PrecIRCalibrationStateError;
    }
    precir_app_show_message(app, header, text, NULL);
    scene_manager_previous_scene(app->scene_manager);
}

void precir_scene_transmit_on_enter(void* context) {
    PrecIRApp* app = context;

    if(precir_transmit_lock(app)) {
        app->transmitting = false;
        app->tx_cancel_requested = false;
        app->tx_success = false;
        app->tx_progress = 0U;
        app->tx_total = 0U;
        furi_mutex_release(app->tx_mutex);
    }

    /* Clear an old cancellation before events can reach the new worker. */
    if(!precir_ir_reset_cancel(app->transmitter)) {
        precir_transmit_start_failed(
            app, "IR busy", "Wait for the current\nIR operation to stop.");
        return;
    }

    if(precir_transmit_lock(app)) {
        app->transmitting = true;
        furi_mutex_release(app->tx_mutex);
    }

    precir_transmit_render(app, false);
    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewWidget);

    FuriThread* thread =
        furi_thread_alloc_ex("PrecIR_TX", PRECIR_TRANSMIT_STACK_SIZE, precir_transmit_worker, app);
    if(!thread) {
        if(precir_transmit_lock(app)) {
            app->transmitting = false;
            furi_mutex_release(app->tx_mutex);
        }
        precir_transmit_start_failed(app, "Out of memory", "Could not start transfer.");
        return;
    }

    /* Publish before start so a fast completion event cannot race the pointer. */
    app->tx_thread = thread;
    furi_thread_start(thread);
}

static void
    precir_transmit_finish_test_clear(PrecIRApp* app, bool cancel_requested, bool success) {
    if(cancel_requested) {
        app->calibration_state = PrecIRCalibrationStateError;
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, PrecIRSceneProfileActions);
        return;
    }

    app->calibration_state = success ? PrecIRCalibrationStateWaiting : PrecIRCalibrationStateError;
    app->calibration_start_tick = furi_get_tick();
    scene_manager_previous_scene(app->scene_manager);
}

static void precir_transmit_finish_regular(PrecIRApp* app, bool cancel_requested, bool success) {
    if(cancel_requested) {
        precir_app_show_message(app, "Cancelled", "IR transfer stopped safely.", NULL);
    } else if(success) {
        notification_message(app->notification, &sequence_success);
        precir_app_show_message(
            app, "Transfer sent", "Keep the tag still and\nwait for its refresh.", NULL);
    } else {
        notification_message(app->notification, &sequence_error);
        precir_app_show_message(
            app, "Transfer failed", "Check alignment and\ntry another setting.", NULL);
    }

    scene_manager_search_and_switch_to_previous_scene(
        app->scene_manager, PrecIRSceneProfileActions);
}

bool precir_scene_transmit_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        bool cancel_requested = false;
        if(precir_transmit_lock(app)) {
            cancel_requested = app->tx_cancel_requested;
            furi_mutex_release(app->tx_mutex);
        }
        precir_transmit_render(app, cancel_requested);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        bool transmitting = false;
        if(precir_transmit_lock(app)) {
            transmitting = app->transmitting;
            if(transmitting) app->tx_cancel_requested = true;
            furi_mutex_release(app->tx_mutex);
        }

        if(app->tx_thread) {
            if(transmitting) precir_ir_stop(app->transmitter);
            precir_transmit_render(app, true);
            return true;
        }
        return false;
    }

    if(event.type != SceneManagerEventTypeCustom || event.event != PrecIREventTransmitDone) {
        return false;
    }

    bool cancel_requested = false;
    bool success = false;
    if(precir_transmit_lock(app)) {
        cancel_requested = app->tx_cancel_requested;
        success = app->tx_success;
        furi_mutex_release(app->tx_mutex);
    }

    precir_transmit_join_thread(app);
    if(app->transmit_kind == PrecIRTransmitKindTestClear) {
        precir_transmit_finish_test_clear(app, cancel_requested, success);
    } else {
        precir_transmit_finish_regular(app, cancel_requested, success);
    }
    return true;
}

void precir_scene_transmit_on_exit(void* context) {
    PrecIRApp* app = context;

    if(app->tx_thread) {
        if(precir_transmit_lock(app)) {
            app->tx_cancel_requested = true;
            furi_mutex_release(app->tx_mutex);
        }
        precir_ir_stop(app->transmitter);
        precir_transmit_join_thread(app);
    }

    if(precir_transmit_lock(app)) {
        app->transmitting = false;
        furi_mutex_release(app->tx_mutex);
    }
    widget_reset(app->widget);
}
