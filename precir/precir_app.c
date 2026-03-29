#include "precir_app.h"

/* ---- View dispatcher navigation callback ---- */

static bool precir_app_back_event_callback(void* context) {
    PrecIRApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void precir_app_tick_event_callback(void* context) {
    PrecIRApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* ---- Alloc / Free ---- */

PrecIRApp* precir_app_alloc(void) {
    PrecIRApp* app = malloc(sizeof(PrecIRApp));
    memset(app, 0, sizeof(*app));

    /* Open services */
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->notification = furi_record_open(RECORD_NOTIFICATION);

    /* Scene manager */
    app->scene_manager = scene_manager_alloc(&precir_scene_handlers, app);

    /* View dispatcher */
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, precir_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, precir_app_tick_event_callback, 200);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* Allocate GUI modules */
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PrecIRViewSubmenu, submenu_get_view(app->submenu));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PrecIRViewTextInput, text_input_get_view(app->text_input));

    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        PrecIRViewVariableItemList,
        variable_item_list_get_view(app->variable_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PrecIRViewWidget, widget_get_view(app->widget));

    app->popup = popup_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PrecIRViewPopup, popup_get_view(app->popup));

    /* IR transmitter */
    app->transmitter = precir_ir_alloc();

    /* Defaults */
    app->display_type = PrecIRDisplayTypeDM;
    app->display_size = PrecIRDisplaySizeMedium;
    app->color_mode = PrecIRColorModeBW;
    app->protocol_mode = PrecIRProtocolPP16;

    app->image_path = furi_string_alloc();

    return app;
}

void precir_app_free(PrecIRApp* app) {
    furi_assert(app);

    /* Free image data */
    if(app->image_data) free(app->image_data);
    if(app->color_data) free(app->color_data);
    furi_string_free(app->image_path);

    /* Free IR transmitter */
    precir_ir_free(app->transmitter);

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

    scene_manager_next_scene(app->scene_manager, PrecIRSceneMainMenu);
    view_dispatcher_run(app->view_dispatcher);

    precir_app_free(app);
    return 0;
}
