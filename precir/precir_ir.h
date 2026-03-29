#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "precir_protocol.h"

/** Opaque IR transmitter context. */
typedef struct PrecIRTransmitter PrecIRTransmitter;

/** Allocate transmitter. */
PrecIRTransmitter* precir_ir_alloc(void);

/** Free transmitter. */
void precir_ir_free(PrecIRTransmitter* tx);

/** Set the protocol mode (PP4 / PP16). */
void precir_ir_set_protocol(PrecIRTransmitter* tx, PrecIRProtocolMode mode);

/** Transmit a single frame with the given repeat count.
 *  @param frame      raw frame bytes (protocol + PLID + cmd + payload + CRC)
 *  @param frame_len  length of frame in bytes
 *  @param repeats    number of times to transmit
 *  @param delay_ms   delay between repeats in milliseconds
 *  Blocks until complete. */
void precir_ir_transmit_frame(
    PrecIRTransmitter* tx,
    const uint8_t* frame,
    size_t frame_len,
    uint16_t repeats,
    uint16_t delay_ms);

/** Send a complete DM image update sequence.
 *  @param plid           4-byte PLID
 *  @param image_data     encoded pixel data (1bpp, padded to 20-byte boundary)
 *  @param image_len      length of image_data in bytes
 *  @param width          display pixel width
 *  @param height         display pixel height
 *  @param compression    0=raw, 2=RLE
 *  @param page           page number (0-15)
 *  @param progress_cb    optional callback(current, total, ctx) for progress
 *  @param progress_ctx   context for progress callback
 *  Returns true on success. */
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
    void* progress_ctx);

/** Send a segment update.
 *  @param plid    4-byte PLID
 *  @param bitmap  23-byte segment bitmap */
void precir_ir_send_segment(
    PrecIRTransmitter* tx,
    const uint8_t plid[4],
    const uint8_t bitmap[PRECIR_SEGMENT_BITMAP]);
