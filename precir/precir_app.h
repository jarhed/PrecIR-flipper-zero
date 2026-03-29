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
#include "precir_scene.h"

#define PRECIR_BARCODE_MAX_LEN 18
#define PRECIR_TEXT_STORE_SIZE  64

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

    /* Image */
    FuriString* image_path;
    uint8_t* image_data;
    size_t image_data_len;
    uint8_t image_compression; /* 0=raw, 2=RLE */
    uint8_t* color_data;      /* second layer for color displays */
    size_t color_data_len;
    uint8_t color_compression; /* 0=raw, 2=RLE */

    /* Segment display */
    uint8_t segment_bitmap[PRECIR_SEGMENT_BITMAP];

    /* Transmission progress */
    bool transmitting;
    uint16_t tx_progress;
    uint16_t tx_total;
} PrecIRApp;

/** Allocate and initialize the application. */
PrecIRApp* precir_app_alloc(void);

/** Free the application and all resources. */
void precir_app_free(PrecIRApp* app);

/** Application entry point (referenced in application.fam). */
int32_t precir_app(void* p);
