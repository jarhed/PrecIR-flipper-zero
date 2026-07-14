#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>

#include "precir_protocol.h"
#include "precir_ir.h"
#include "precir_image.h"
#include "precir_profiles.h"
#include "precir_scene.h"

#define PRECIR_BARCODE_MAX_LEN 18
#define PRECIR_TEXT_STORE_SIZE 64

/* ---- View IDs ---- */

typedef enum {
    PrecIRViewSubmenu,
    PrecIRViewTextInput,
    PrecIRViewVariableItemList,
    PrecIRViewWidget,
    PrecIRViewPopup,
} PrecIRView;

/* ---- Application state ---- */

typedef struct {
    /* Services */
    Gui* gui;
    Storage* storage;
    DialogsApp* dialogs;
    NotificationApp* notification;

    /* Scene / view management */
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;

    /* GUI modules */
    Submenu* submenu;
    TextInput* text_input;
    VariableItemList* variable_item_list;
    Widget* widget;
    Popup* popup;

    /* Saved tags.  The on-disk format is intentionally stable (PCRP v1). */
    PrecIRProfileStore profiles;
    int8_t active_profile;
    bool profile_list_calibration;
    bool pending_auto_calibrate;
    bool auto_calibrate;

    /* IR transmitter */
    PrecIRTransmitter* transmitter;

    /* User-entered barcode and derived PLID */
    char barcode[PRECIR_BARCODE_MAX_LEN];
    char text_store[PRECIR_TEXT_STORE_SIZE];
    uint8_t plid[4];
    bool plid_valid;

    /* Display configuration */
    PrecIRDisplayType display_type;
    PrecIRDisplaySize display_size;
    PrecIRColorMode color_mode;
    PrecIRProtocolMode protocol_mode;
    uint8_t display_page;

    /* Image */
    FuriString* image_path;
    uint8_t* image_data;
    size_t image_data_len;
    uint8_t image_compression; /* 0=raw, 2=RLE */

    /* Current action / calibration workflow. */
    PrecIRTransmitKind transmit_kind;
    uint8_t calibration_index;
    PrecIRCalibrationState calibration_state;
    uint32_t calibration_start_tick;

    /* Segment display */
    uint8_t segment_bitmap[PRECIR_SEGMENT_BITMAP];

    /* Transmission progress */
    FuriThread* tx_thread;
    FuriMutex* tx_mutex;
    bool transmitting;
    bool tx_cancel_requested;
    bool tx_success;
    uint16_t tx_progress;
    uint16_t tx_total;
} PrecIRApp;

/** Return the active saved profile, or NULL when none is selected. */
PrecIRProfile* precir_app_active_profile(PrecIRApp* app);

/** Load the active profile into the editable runtime fields. */
bool precir_app_load_active_profile(PrecIRApp* app);

/** Commit runtime settings to the active profile and save them transactionally. */
bool precir_app_commit_active_settings(PrecIRApp* app);

/** Release the currently prepared image payload and clear its metadata. */
void precir_app_free_image(PrecIRApp* app);

/** Decode and prepare a BMP using the current display settings. */
bool precir_app_load_image(PrecIRApp* app, const char* path);

/** Prepare an all-white payload using the current display settings. */
bool precir_app_make_white_image(PrecIRApp* app);

/** Show a small modal message with one right-side button. */
DialogMessageButton precir_app_show_message(
    PrecIRApp* app,
    const char* title,
    const char* text,
    const char* right_button);

/** Allocate and initialize the application. */
PrecIRApp* precir_app_alloc(void);

/** Free the application and all resources. */
void precir_app_free(PrecIRApp* app);

/** Application entry point (referenced in application.fam). */
int32_t precir_app(void* p);
