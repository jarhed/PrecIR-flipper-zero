#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <storage/storage.h>
#include "precir_protocol.h"

/** Image layer for color displays. */
typedef enum {
    PrecIRImageLayerBW,   /* black/white layer */
    PrecIRImageLayerColor, /* red/color overlay layer */
} PrecIRImageLayer;

/** Load a BMP file and convert to 1-bit-per-pixel ESL format.
 *
 *  @param storage     Flipper storage instance
 *  @param path        path to BMP file on SD card
 *  @param target_w    target display width in pixels
 *  @param target_h    target display height in pixels
 *  @param layer       which layer to extract (BW or Color)
 *  @param out_data    receives malloc'd pixel data (caller frees)
 *  @param out_len     receives data length in bytes
 *  Returns true on success. */
bool precir_image_load_bmp(
    Storage* storage,
    const char* path,
    uint16_t target_w,
    uint16_t target_h,
    PrecIRImageLayer layer,
    uint8_t** out_data,
    size_t* out_len);

/** RLE-compress image data (unary run-length encoding).
 *
 *  @param raw         raw 1bpp pixel data
 *  @param raw_len     length in bytes
 *  @param out_data    receives malloc'd compressed data (caller frees)
 *  @param out_len     receives compressed length in bytes
 *  Returns true if compression succeeded and is smaller than raw. */
bool precir_image_rle_compress(
    const uint8_t* raw,
    size_t raw_len,
    uint8_t** out_data,
    size_t* out_len);

/** Pad data to a multiple of PRECIR_DATA_PER_FRAME (20) bytes.
 *  Modifies *data and *len in place (realloc). */
void precir_image_pad(uint8_t** data, size_t* len);

/** Check if an image file has color content (non-B&W pixels).
 *  Quick scan — reads the BMP and checks for red/colored pixels. */
bool precir_image_has_color(Storage* storage, const char* path);
