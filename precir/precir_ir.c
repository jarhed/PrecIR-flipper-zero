#include "precir_ir.h"

#include <furi.h>
#include <furi_hal_infrared.h>
#include <stdlib.h>
#include <string.h>

/* ---- PP16 symbol total durations (microseconds) for nibbles 0..15 ---- */
/* Formula: burst(21us) + gap = 21 + (N+1)*8 */
static const uint32_t pp16_symbol_us[16] = {
    29, 37, 45, 53, 61, 69, 77, 85,
    93, 101, 109, 117, 125, 133, 141, 149,
};

/* ---- PP4 symbol total durations and burst ---- */
static const uint32_t pp4_symbol_us[4] = {
    PRECIR_PP4_SYM0_US, /* 00 → 61 */
    PRECIR_PP4_SYM1_US, /* 01 → 244 */
    PRECIR_PP4_SYM2_US, /* 10 → 122 */
    PRECIR_PP4_SYM3_US, /* 11 → 183 */
};

/* ---- PP16 header bytes prepended to every PP16 frame ---- */
static const uint8_t pp16_header[PRECIR_PP16_HEADER_LEN] = {0x00, 0x00, 0x00, 0x40};

/* ---- Transmitter context ---- */

struct PrecIRTransmitter {
    PrecIRProtocolMode mode;

    /* ISR callback state */
    const uint8_t* frame;
    size_t frame_len;
    size_t byte_pos;
    uint8_t symbol_idx; /* which symbol within current byte (0..1 for PP16, 0..3 for PP4) */
    bool sending_burst; /* true = next value is mark, false = space */

    FuriSemaphore* done_sem;
};

/* ---- HAL ISR callback ---- */

static FuriHalInfraredTxGetDataState
    precir_ir_isr_callback(void* context, uint32_t* duration, bool* level) {
    PrecIRTransmitter* tx = context;

    /* If we've exhausted all bytes, signal done */
    if(tx->byte_pos >= tx->frame_len) {
        *duration = 0;
        *level = false;
        return FuriHalInfraredTxGetDataStateLastDone;
    }

    uint8_t byte_val = tx->frame[tx->byte_pos];

    if(tx->mode == PrecIRProtocolPP16) {
        /* PP16: 2 nibbles per byte, high nibble first */
        uint8_t nibble;
        if(tx->symbol_idx == 0) {
            nibble = (byte_val >> 4) & 0x0F;
        } else {
            nibble = byte_val & 0x0F;
        }

        if(tx->sending_burst) {
            /* Mark (carrier ON) */
            *duration = PRECIR_PP16_BURST_US;
            *level = true;
            tx->sending_burst = false;
        } else {
            /* Space (carrier OFF) */
            uint32_t total = pp16_symbol_us[nibble];
            *duration = total - PRECIR_PP16_BURST_US;
            *level = false;
            tx->sending_burst = true;

            /* Advance to next symbol */
            tx->symbol_idx++;
            if(tx->symbol_idx >= 2) {
                tx->symbol_idx = 0;
                tx->byte_pos++;
            }
        }
    } else {
        /* PP4: 4 crumbs per byte, MSB first */
        uint8_t shift = (3 - tx->symbol_idx) * 2; /* 6, 4, 2, 0 */
        uint8_t crumb = (byte_val >> shift) & 0x03;

        if(tx->sending_burst) {
            *duration = PRECIR_PP4_BURST_US;
            *level = true;
            tx->sending_burst = false;
        } else {
            uint32_t total = pp4_symbol_us[crumb];
            *duration = total - PRECIR_PP4_BURST_US;
            *level = false;
            tx->sending_burst = true;

            tx->symbol_idx++;
            if(tx->symbol_idx >= 4) {
                tx->symbol_idx = 0;
                tx->byte_pos++;
            }
        }
    }

    /* Check if this was the very last symbol's space */
    if(!(*level) && tx->byte_pos >= tx->frame_len) {
        return FuriHalInfraredTxGetDataStateDone;
    }

    return FuriHalInfraredTxGetDataStateOk;
}

static void precir_ir_isr_done_callback(void* context) {
    PrecIRTransmitter* tx = context;
    furi_semaphore_release(tx->done_sem);
}

/* ---- Public API ---- */

PrecIRTransmitter* precir_ir_alloc(void) {
    PrecIRTransmitter* tx = malloc(sizeof(PrecIRTransmitter));
    memset(tx, 0, sizeof(PrecIRTransmitter));
    tx->mode = PrecIRProtocolPP16;
    tx->done_sem = furi_semaphore_alloc(1, 0);
    return tx;
}

void precir_ir_free(PrecIRTransmitter* tx) {
    furi_assert(tx);
    furi_semaphore_free(tx->done_sem);
    free(tx);
}

void precir_ir_set_protocol(PrecIRTransmitter* tx, PrecIRProtocolMode mode) {
    furi_assert(tx);
    tx->mode = mode;
}

/** Transmit a raw byte buffer once via the HAL async IR TX. */
static void precir_ir_transmit_once(PrecIRTransmitter* tx, const uint8_t* data, size_t len) {
    tx->frame = data;
    tx->frame_len = len;
    tx->byte_pos = 0;
    tx->symbol_idx = 0;
    tx->sending_burst = true;

    furi_hal_infrared_async_tx_set_data_isr_callback(precir_ir_isr_callback, tx);
    furi_hal_infrared_async_tx_set_signal_sent_isr_callback(precir_ir_isr_done_callback, tx);

    furi_hal_infrared_async_tx_start(PRECIR_CARRIER_FREQ, PRECIR_DUTY_CYCLE);
    furi_hal_infrared_async_tx_wait_termination();

    /* Wait for the done callback */
    furi_semaphore_acquire(tx->done_sem, FuriWaitForever);
}

void precir_ir_transmit_frame(
    PrecIRTransmitter* tx,
    const uint8_t* frame,
    size_t frame_len,
    uint16_t repeats,
    uint16_t delay_ms) {
    furi_assert(tx);
    furi_assert(frame);

    if(tx->mode == PrecIRProtocolPP16) {
        /* PP16 frames need a 4-byte header prepended */
        size_t total_len = PRECIR_PP16_HEADER_LEN + frame_len;
        uint8_t* buf = malloc(total_len);
        memcpy(buf, pp16_header, PRECIR_PP16_HEADER_LEN);
        memcpy(buf + PRECIR_PP16_HEADER_LEN, frame, frame_len);

        for(uint16_t i = 0; i < repeats; i++) {
            precir_ir_transmit_once(tx, buf, total_len);
            if(delay_ms > 0 && i < (repeats - 1)) {
                furi_delay_ms(delay_ms);
            }
        }

        free(buf);
    } else {
        /* PP4: transmit frame bytes directly */
        for(uint16_t i = 0; i < repeats; i++) {
            precir_ir_transmit_once(tx, frame, frame_len);
            if(delay_ms > 0 && i < (repeats - 1)) {
                furi_delay_ms(delay_ms);
            }
        }
    }
}

bool precir_ir_send_image(
    PrecIRTransmitter* tx,
    const uint8_t plid[4],
    const uint8_t* image_data,
    size_t image_len,
    uint16_t width,
    uint16_t height,
    uint8_t compression,
    uint8_t page,
    void (*progress_cb)(uint16_t current, uint16_t total, void* ctx),
    void* progress_ctx) {
    furi_assert(tx);
    furi_assert(plid);
    furi_assert(image_data);

    uint8_t frame_buf[PRECIR_FRAME_MAX];
    size_t flen;

    /* 1. Wake-up frame: 400 repeats, ~25ms between each */
    flen = precir_build_wake_frame(frame_buf, plid);
    precir_ir_transmit_frame(tx, frame_buf, flen, PRECIR_WAKE_REPEATS, 25);

    /* 2. Params frame: 1 repeat */
    flen = precir_build_params_frame(
        frame_buf, plid, (uint16_t)image_len, compression, page, width, height);
    precir_ir_transmit_frame(tx, frame_buf, flen, 1, 0);

    /* 3. Data frames: one per 20-byte chunk */
    uint16_t total_frames = (uint16_t)((image_len + PRECIR_DATA_PER_FRAME - 1) / PRECIR_DATA_PER_FRAME);

    for(uint16_t i = 0; i < total_frames; i++) {
        size_t offset = (size_t)i * PRECIR_DATA_PER_FRAME;
        size_t chunk_len = image_len - offset;
        if(chunk_len > PRECIR_DATA_PER_FRAME) {
            chunk_len = PRECIR_DATA_PER_FRAME;
        }

        flen = precir_build_data_frame(frame_buf, plid, i, image_data + offset, chunk_len);
        precir_ir_transmit_frame(tx, frame_buf, flen, 1, 0);

        if(progress_cb) {
            progress_cb(i + 1, total_frames, progress_ctx);
        }
    }

    /* 4. Refresh frame: 1 repeat */
    flen = precir_build_refresh_frame(frame_buf, plid);
    precir_ir_transmit_frame(tx, frame_buf, flen, 1, 0);

    return true;
}

void precir_ir_send_segment(
    PrecIRTransmitter* tx,
    const uint8_t plid[4],
    const uint8_t bitmap[PRECIR_SEGMENT_BITMAP]) {
    furi_assert(tx);
    furi_assert(plid);
    furi_assert(bitmap);

    uint8_t frame_buf[PRECIR_FRAME_MAX];
    size_t flen = precir_build_segment_frame(frame_buf, plid, bitmap);
    precir_ir_transmit_frame(tx, frame_buf, flen, 1, 0);
}
