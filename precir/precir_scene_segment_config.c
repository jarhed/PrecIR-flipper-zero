#include "precir_app.h"

/** Parse a single hex character to its 4-bit value.
 *  Returns -1 on invalid input. */
static int8_t hex_nibble(char c) {
    if(c >= '0' && c <= '9') return (int8_t)(c - '0');
    if(c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
    if(c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
    return -1;
}

/** Parse a hex string into a byte buffer.
 *  @param hex    hex string (must contain exactly num_bytes*2 hex chars)
 *  @param out    output buffer
 *  @param num_bytes  expected number of bytes
 *  Returns true on success. */
static bool parse_hex_string(const char* hex, uint8_t* out, size_t num_bytes) {
    for(size_t i = 0; i < num_bytes; i++) {
        int8_t hi = hex_nibble(hex[i * 2]);
        int8_t lo = hex_nibble(hex[i * 2 + 1]);
        if(hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool precir_segment_config_validator(const char* text, FuriString* error, void* context) {
    UNUSED(context);

    size_t length = 0;
    while(length <= PRECIR_SEGMENT_BITMAP * 2U && text[length] != '\0') {
        length++;
    }
    if(length != PRECIR_SEGMENT_BITMAP * 2U) {
        furi_string_set(error, "Enter exactly 46 hex characters");
        return false;
    }

    for(size_t i = 0; i < length; i++) {
        if(hex_nibble(text[i]) < 0) {
            furi_string_set(error, "Use hexadecimal digits 0-9, A-F");
            return false;
        }
    }

    return true;
}

/** Text input callback. */
static void precir_segment_config_input_callback(void* context) {
    PrecIRApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, PrecIREventConfigDone);
}

/** on_enter: set up text input for hex segment bitmap. */
void precir_scene_segment_config_on_enter(void* context) {
    PrecIRApp* app = context;

    app->text_store[0] = '\0';
    app->protocol_mode = PrecIRProtocolPP4;

    text_input_set_header_text(app->text_input, "Segment Hex (46 chars)");
    text_input_set_minimum_length(app->text_input, PRECIR_SEGMENT_BITMAP * 2U);
    text_input_set_validator(app->text_input, precir_segment_config_validator, app);
    text_input_set_result_callback(
        app->text_input,
        precir_segment_config_input_callback,
        app,
        app->text_store,
        PRECIR_TEXT_STORE_SIZE,
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, PrecIRViewTextInput);
}

/** on_event: parse hex input and proceed to transmit. */
bool precir_scene_segment_config_on_event(void* context, SceneManagerEvent event) {
    PrecIRApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PrecIREventConfigDone) {
            /* Validate length: need exactly 46 hex chars for 23 bytes */
            size_t len = strlen(app->text_store);
            if(len == PRECIR_SEGMENT_BITMAP * 2 &&
               parse_hex_string(app->text_store, app->segment_bitmap, PRECIR_SEGMENT_BITMAP)) {
                app->protocol_mode = PrecIRProtocolPP4;
                app->transmit_kind = PrecIRTransmitKindSegments;
                scene_manager_next_scene(app->scene_manager, PrecIRSceneTransmit);
            } else {
                /* Defensive fallback; the text-input validator normally catches this. */
                memset(app->segment_bitmap, 0, PRECIR_SEGMENT_BITMAP);
                app->text_store[0] = '\0';
                text_input_set_header_text(app->text_input, "Invalid hex - retry");
            }
            consumed = true;
        }
    }

    return consumed;
}

/** on_exit: reset text input. */
void precir_scene_segment_config_on_exit(void* context) {
    PrecIRApp* app = context;
    text_input_reset(app->text_input);
}
