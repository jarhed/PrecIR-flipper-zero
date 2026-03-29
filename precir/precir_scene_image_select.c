#include "precir_app.h"

/** on_enter: show file browser for BMP selection, then load image. */
void precir_scene_image_select_on_enter(void* context) {
    PrecIRApp* app = context;

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, ".bmp", NULL);
    browser_options.base_path = STORAGE_EXT_PATH_PREFIX;
    browser_options.hide_ext = false;

    FuriString* path = app->image_path;
    if(furi_string_empty(path)) {
        furi_string_set_str(path, STORAGE_EXT_PATH_PREFIX);
    }

    bool selected = dialog_file_browser_show(app->dialogs, path, path, &browser_options);

    if(selected) {
        const char* path_cstr = furi_string_get_cstr(path);
        uint16_t width = precir_display_width(app->display_size);
        uint16_t height = precir_display_height(app->display_size);

        /* Free any previous image data */
        if(app->image_data) {
            free(app->image_data);
            app->image_data = NULL;
            app->image_data_len = 0;
        }
        if(app->color_data) {
            free(app->color_data);
            app->color_data = NULL;
            app->color_data_len = 0;
        }

        /* Load BW layer */
        uint8_t* bw_data = NULL;
        size_t bw_len = 0;
        bool ok =
            precir_image_load_bmp(app->storage, path_cstr, width, height, PrecIRImageLayerBW, &bw_data, &bw_len);

        if(!ok) {
            scene_manager_previous_scene(app->scene_manager);
            return;
        }

        /* Try RLE compression on BW layer */
        uint8_t* compressed = NULL;
        size_t compressed_len = 0;
        if(precir_image_rle_compress(bw_data, bw_len, &compressed, &compressed_len)) {
            /* Compressed is smaller -- use it */
            free(bw_data);
            app->image_data = compressed;
            app->image_data_len = compressed_len;
            app->image_compression = 2; /* RLE */
        } else {
            /* Raw is better or same */
            if(compressed) free(compressed);
            app->image_data = bw_data;
            app->image_data_len = bw_len;
            app->image_compression = 0; /* raw */
        }

        /* Pad to multiple of PRECIR_DATA_PER_FRAME */
        precir_image_pad(&app->image_data, &app->image_data_len);

        /* Load color layer if needed */
        if(app->color_mode != PrecIRColorModeBW) {
            uint8_t* col_data = NULL;
            size_t col_len = 0;
            if(precir_image_load_bmp(
                   app->storage, path_cstr, width, height, PrecIRImageLayerColor, &col_data, &col_len)) {
                /* Try RLE on color layer too */
                uint8_t* col_compressed = NULL;
                size_t col_compressed_len = 0;
                if(precir_image_rle_compress(col_data, col_len, &col_compressed, &col_compressed_len)) {
                    free(col_data);
                    app->color_data = col_compressed;
                    app->color_data_len = col_compressed_len;
                    app->color_compression = 2; /* RLE */
                } else {
                    if(col_compressed) free(col_compressed);
                    app->color_data = col_data;
                    app->color_data_len = col_len;
                    app->color_compression = 0; /* raw */
                }

                precir_image_pad(&app->color_data, &app->color_data_len);
            }
        }

        scene_manager_next_scene(app->scene_manager, PrecIRSceneTransmit);
    } else {
        scene_manager_previous_scene(app->scene_manager);
    }
}

/** on_event: handle back navigation. */
bool precir_scene_image_select_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PrecIREventBack) {
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    }

    return consumed;
}

/** on_exit: nothing to clean up. */
void precir_scene_image_select_on_exit(void* context) {
    UNUSED(context);
}
