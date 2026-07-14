#include "precir_app.h"

/* ---- View dispatcher navigation callback ---- */

static bool precir_app_back_event_callback(void* context) {
    PrecIRApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static bool precir_app_custom_event_callback(void* context, uint32_t event) {
    PrecIRApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static void precir_app_tick_event_callback(void* context) {
    PrecIRApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* ---- Shared profile / image helpers ---- */

PrecIRProfile* precir_app_active_profile(PrecIRApp* app) {
    if(!app || app->active_profile < 0 || (uint8_t)app->active_profile >= app->profiles.count) {
        return NULL;
    }

    return &app->profiles.profiles[(uint8_t)app->active_profile];
}

bool precir_app_load_active_profile(PrecIRApp* app) {
    PrecIRProfile* profile = precir_app_active_profile(app);
    if(!profile || !precir_profile_is_valid(profile)) return false;

    memcpy(app->barcode, profile->barcode, sizeof(app->barcode));
    app->plid_valid = precir_plid_from_barcode(app->barcode, app->plid);
    if(!app->plid_valid) return false;

    app->display_type = PrecIRDisplayTypeDM;
    app->display_size = (PrecIRDisplaySize)profile->display_size;
    app->color_mode = (PrecIRColorMode)profile->color_mode;
    app->protocol_mode = (PrecIRProtocolMode)profile->protocol_mode;
    app->display_page = profile->display_page;
    furi_string_set_str(app->image_path, profile->bmp_path);
    return true;
}

bool precir_app_commit_active_settings(PrecIRApp* app) {
    PrecIRProfile* profile = precir_app_active_profile(app);
    if(!profile || app->display_size > PrecIRDisplaySizeLarge ||
       app->color_mode > PrecIRColorMode4C || app->protocol_mode > PrecIRProtocolPP16 ||
       app->display_page > 15U) {
        return false;
    }

    const PrecIRProfile backup = *profile;
    profile->display_size = (uint8_t)app->display_size;
    profile->color_mode = (uint8_t)app->color_mode;
    profile->protocol_mode = (uint8_t)app->protocol_mode;
    profile->display_page = app->display_page;

    if(!precir_profile_is_valid(profile) || !precir_profiles_save(app->storage, &app->profiles)) {
        *profile = backup;
        return false;
    }

    return true;
}

void precir_app_free_image(PrecIRApp* app) {
    if(!app) return;
    free(app->image_data);
    app->image_data = NULL;
    app->image_data_len = 0U;
    app->image_compression = 0U;
}

static void precir_app_take_image_payload(PrecIRApp* app, PrecIRImagePayload* payload) {
    precir_app_free_image(app);
    app->image_data = payload->data;
    app->image_data_len = payload->data_len;
    app->image_compression = payload->compression;
    payload->data = NULL;
    payload->data_len = 0U;
    payload->compression = 0U;
}

bool precir_app_load_image(PrecIRApp* app, const char* path) {
    if(!app || !path || path[0] == '\0') return false;

    PrecIRImagePayload payload = {0};
    if(!precir_image_load_bmp_payload(
           app->storage,
           path,
           precir_display_width(app->display_size),
           precir_display_height(app->display_size),
           app->color_mode,
           &payload)) {
        return false;
    }

    precir_app_take_image_payload(app, &payload);
    /* The file browser may pass a pointer into image_path itself. Avoid
     * assigning that string to itself because its backing buffer may move. */
    if(strcmp(furi_string_get_cstr(app->image_path), path) != 0) {
        furi_string_set_str(app->image_path, path);
    }
    return true;
}

bool precir_app_make_white_image(PrecIRApp* app) {
    if(!app) return false;

    PrecIRImagePayload payload = {0};
    if(!precir_image_make_white_payload(
           precir_display_width(app->display_size),
           precir_display_height(app->display_size),
           app->color_mode,
           &payload)) {
        return false;
    }

    precir_app_take_image_payload(app, &payload);
    return true;
}

DialogMessageButton precir_app_show_message(
    PrecIRApp* app,
    const char* title,
    const char* text,
    const char* right_button) {
    if(!app || !app->dialogs) return DialogMessageButtonBack;

    DialogMessage* message = dialog_message_alloc();
    if(!message) return DialogMessageButtonBack;

    dialog_message_set_header(message, title ? title : "PrecIR", 64, 6, AlignCenter, AlignCenter);
    dialog_message_set_text(message, text ? text : "", 64, 30, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, NULL, NULL, right_button ? right_button : "OK");
    const DialogMessageButton result = dialog_message_show(app->dialogs, message);
    dialog_message_free(message);
    return result;
}

/* ---- Alloc / Free ---- */

PrecIRApp* precir_app_alloc(void) {
    PrecIRApp* app = malloc(sizeof(PrecIRApp));
    if(!app) return NULL;
    memset(app, 0, sizeof(*app));

    if(!precir_protocol_self_test() || !precir_image_self_test() || !precir_ir_self_test()) {
        FURI_LOG_E("PrecIR", "Protocol, image, or waveform self-test failed");
        free(app);
        return NULL;
    }

    /* Open services */
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->notification = furi_record_open(RECORD_NOTIFICATION);

    /* Allocate scene, view, and application resources before registering views. */
    app->scene_manager = scene_manager_alloc(&precir_scene_handlers, app);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->text_input = text_input_alloc();
    app->variable_item_list = variable_item_list_alloc();
    app->widget = widget_alloc();
    app->popup = popup_alloc();
    app->transmitter = precir_ir_alloc();
    app->tx_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->image_path = furi_string_alloc();

    if(!app->gui || !app->storage || !app->dialogs || !app->notification || !app->scene_manager ||
       !app->view_dispatcher || !app->submenu || !app->text_input || !app->variable_item_list ||
       !app->widget || !app->popup || !app->transmitter || !app->tx_mutex || !app->image_path) {
        FURI_LOG_E("PrecIR", "Application allocation failed");
        if(app->image_path) furi_string_free(app->image_path);
        if(app->tx_mutex) furi_mutex_free(app->tx_mutex);
        if(app->transmitter) precir_ir_free(app->transmitter);
        if(app->popup) popup_free(app->popup);
        if(app->widget) widget_free(app->widget);
        if(app->variable_item_list) variable_item_list_free(app->variable_item_list);
        if(app->text_input) text_input_free(app->text_input);
        if(app->submenu) submenu_free(app->submenu);
        if(app->scene_manager) scene_manager_free(app->scene_manager);
        if(app->view_dispatcher) view_dispatcher_free(app->view_dispatcher);
        if(app->notification) furi_record_close(RECORD_NOTIFICATION);
        if(app->dialogs) furi_record_close(RECORD_DIALOGS);
        if(app->storage) furi_record_close(RECORD_STORAGE);
        if(app->gui) furi_record_close(RECORD_GUI);
        free(app);
        return NULL;
    }

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, precir_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, precir_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, precir_app_tick_event_callback, 200);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    view_dispatcher_add_view(
        app->view_dispatcher, PrecIRViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, PrecIRViewTextInput, text_input_get_view(app->text_input));
    view_dispatcher_add_view(
        app->view_dispatcher,
        PrecIRViewVariableItemList,
        variable_item_list_get_view(app->variable_item_list));
    view_dispatcher_add_view(app->view_dispatcher, PrecIRViewWidget, widget_get_view(app->widget));
    view_dispatcher_add_view(app->view_dispatcher, PrecIRViewPopup, popup_get_view(app->popup));

    /* Selection 1 is the known-good default and the first calibration
     * candidate: 296x128, B/W/red, PP4, page 0. */
    precir_profiles_init(&app->profiles);
    if(!precir_profiles_load(app->storage, &app->profiles)) {
        FURI_LOG_W("PrecIR", "Saved profile file is invalid; using an empty store");
        precir_app_show_message(
            app, "Profiles reset", "The saved profile file was\ninvalid and was reset.", "OK");
    }
    app->active_profile = -1;
    app->display_type = PrecIRDisplayTypeDM;
    app->display_size = PrecIRDisplaySizeLarge;
    app->color_mode = PrecIRColorModeBWR;
    app->protocol_mode = PrecIRProtocolPP4;
    app->display_page = 0U;
    app->transmit_kind = PrecIRTransmitKindImage;
    app->calibration_state = PrecIRCalibrationStateIdle;

    return app;
}

void precir_app_free(PrecIRApp* app) {
    furi_assert(app);

    if(app->tx_thread) {
        precir_ir_stop(app->transmitter);
        furi_thread_join(app->tx_thread);
        furi_thread_free(app->tx_thread);
        app->tx_thread = NULL;
    }

    /* Free image data */
    precir_app_free_image(app);
    furi_string_free(app->image_path);

    /* Free IR transmitter */
    precir_ir_free(app->transmitter);
    furi_mutex_free(app->tx_mutex);

    /* Remove and free views */
    view_dispatcher_remove_view(app->view_dispatcher, PrecIRViewPopup);
    popup_free(app->popup);

    view_dispatcher_remove_view(app->view_dispatcher, PrecIRViewWidget);
    widget_free(app->widget);

    view_dispatcher_remove_view(app->view_dispatcher, PrecIRViewVariableItemList);
    variable_item_list_free(app->variable_item_list);

    view_dispatcher_remove_view(app->view_dispatcher, PrecIRViewTextInput);
    text_input_free(app->text_input);

    view_dispatcher_remove_view(app->view_dispatcher, PrecIRViewSubmenu);
    submenu_free(app->submenu);

    /* Free scene manager and view dispatcher */
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    /* Close services */
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);

    free(app);
}

/* ---- Entry point ---- */

int32_t precir_app(void* p) {
    UNUSED(p);

    PrecIRApp* app = precir_app_alloc();
    if(!app) return -1;

    scene_manager_next_scene(app->scene_manager, PrecIRSceneMainMenu);
    view_dispatcher_run(app->view_dispatcher);

    precir_app_free(app);
    return 0;
}
