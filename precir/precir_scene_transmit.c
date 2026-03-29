#include "precir_app.h"

#define PRECIR_TRANSMIT_STACK_SIZE  2048
#define PRECIR_PROGRESS_INTERVAL_MS 200
#define PRECIR_DONE_POPUP_MS        1500

/* ---- Progress callback (called from transmit thread) ---- */

static void precir_transmit_progress_cb(uint16_t current, uint16_t total, void* ctx) {
    PrecIRApp* app = ctx;
    app->tx_progress = current;
    app->tx_total = total;
}

/* ---- Transmit thread ---- */

static int32_t precir_transmit_thread(void* context) {
    PrecIRApp* app = context;
    precir_ir_set_protocol(app->transmitter, app->protocol_mode);

    if(app->display_type == PrecIRDisplayTypeDM) {
        uint16_t w = precir_display_width(app->display_size);
        uint16_t h = precir_display_height(app->display_size);

        /* Send BW layer */
        precir_ir_send_image(
            app->transmitter,
            app->plid,
            app->image_data,
            app->image_data_len,
            w,
            h,
            app->image_compression,
            0, /* page=0 */
            precir_transmit_progress_cb,
            app);

        /* If color, send color layer too */
        if(app->color_data && app->color_data_len > 0) {
            precir_ir_send_image(
                app->transmitter,
                app->plid,
                app->color_data,
                app->color_data_len,
                w,
                h,
                app->color_compression,
                1, /* page=1 for color layer */
                precir_transmit_progress_cb,
                app);
        }
    } else {
        precir_ir_send_segment(app->transmitter, app->plid, app->segment_bitmap);
    }

    /* Signal completion */
    view_dispatcher_send_custom_event(app->view_dispatcher, PrecIREventTransmitDone);
    return 0;
}

/* ---- Timer callback: refresh progress widget ---- */

static void precir_transmit_timer_cb(void* context) {
    PrecIRApp* app = context;
    widget_reset(app->widget);

    char buf[64];
    if(app->tx_total > 0) {
        snprintf(buf, sizeof(buf), "Frame %u / %u", app->tx_progress, app->tx_total);
    } else {
        snprintf(buf, sizeof(buf), "Preparing...");
    }

    widget_add_string_element(
        app->widget, 64, 10, AlignCenter, AlignTop, FontPrimary, "Transmitting...");
    widget_add_string_element(
        app->widget, 64, 32, AlignCenter, AlignTop, FontSecondary, buf);
}

/* ---- Scene handlers ---- */

void precir_scene_transmit_on_enter(void* context) {
    PrecIRApp* app = context;

    /* Reset progress counters */
    app->tx_progress = 0;
    app->tx_total = 0;
    app->transmitting = true;

    /* Initial widget content */
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 10, AlignCenter, AlignTop, FontPrimary, "Transmitting...");
    widget_add_string_element(
        app->widget, 64, 32, AlignCenter, AlignTop, FontSecondary, "Preparing...");
    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewWidget);

    /* Start periodic timer for progress updates */
    FuriTimer* timer = furi_timer_alloc(precir_transmit_timer_cb, FuriTimerTypePeriodic, app);
    scene_manager_set_scene_state(
        app->scene_manager, PrecIRSceneTransmit, (uint32_t)(uintptr_t)timer);
    furi_timer_start(timer, furi_ms_to_ticks(PRECIR_PROGRESS_INTERVAL_MS));

    /* Create and start transmit thread */
    FuriThread* thread = furi_thread_alloc_ex("PrecIR_TX", PRECIR_TRANSMIT_STACK_SIZE, precir_transmit_thread, app);
    /* Store thread pointer via scene_manager secondary state -- use widget user data workaround:
       We pack both pointers by storing the timer in scene state and thread in a tagged field. */
    app->transmitting = true; /* double-set for clarity */
    furi_thread_start(thread);

    /* Stash thread pointer in the widget element area (we retrieve it in on_exit).
       Use a simple approach: store in text_store which is not used during transmit. */
    memcpy(app->text_store, &thread, sizeof(FuriThread*));
}

bool precir_scene_transmit_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PrecIREventTransmitDone) {
            app->transmitting = false;

            /* Stop the progress timer */
            FuriTimer* timer = (FuriTimer*)(uintptr_t)scene_manager_get_scene_state(
                app->scene_manager, PrecIRSceneTransmit);
            if(timer) {
                furi_timer_stop(timer);
            }

            /* Join the thread */
            FuriThread* thread;
            memcpy(&thread, app->text_store, sizeof(FuriThread*));
            furi_thread_join(thread);
            furi_thread_free(thread);
            memset(app->text_store, 0, sizeof(FuriThread*));

            /* Show "Done!" popup */
            popup_set_header(app->popup, "Done!", 64, 26, AlignCenter, AlignCenter);
            popup_set_timeout(app->popup, PRECIR_DONE_POPUP_MS);
            popup_enable_timeout(app->popup);
            popup_set_context(app->popup, app);
            popup_set_callback(app->popup, NULL);
            view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewPopup);

            /* After timeout, go back to main menu */
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, PrecIRSceneMainMenu);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        /* User pressed back during transmission */
        app->transmitting = false;
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }

    return consumed;
}

void precir_scene_transmit_on_exit(void* context) {
    PrecIRApp* app = context;

    /* Stop and free the progress timer */
    FuriTimer* timer = (FuriTimer*)(uintptr_t)scene_manager_get_scene_state(
        app->scene_manager, PrecIRSceneTransmit);
    if(timer) {
        furi_timer_stop(timer);
        furi_timer_free(timer);
        scene_manager_set_scene_state(app->scene_manager, PrecIRSceneTransmit, 0);
    }

    /* If thread is still running (back pressed), wait for it to finish and free */
    FuriThread* thread;
    memcpy(&thread, app->text_store, sizeof(FuriThread*));
    if(thread) {
        furi_thread_join(thread);
        furi_thread_free(thread);
        memset(app->text_store, 0, sizeof(FuriThread*));
    }

    app->transmitting = false;
    widget_reset(app->widget);
    popup_reset(app->popup);
}
