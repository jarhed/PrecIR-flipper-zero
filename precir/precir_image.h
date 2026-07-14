#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <storage/storage.h>

#include "precir_protocol.h"

/** Fully prepared image stream ready for PrecIR data frames. */
typedef struct {
    uint8_t* data;
    size_t data_len;
    uint8_t compression; /* 0=raw, 2=RLE */
} PrecIRImagePayload;

/** Load, crop, and scale a BMP into the ESL's row-major bitplane format.
 *
 * Only uncompressed 24-bit and 32-bit BMP files are accepted. Pixels are
 * packed MSB first. Color images contain two complete, consecutive planes:
 * the black/white plane followed by the red/yellow selector plane.
 *
 * @param storage     Flipper storage instance
 * @param path        path to a BMP file on the SD card
 * @param target_w    output width in pixels
 * @param target_h    output height in pixels
 * @param color_mode  one plane for BW, two planes for BWR/4-color
 * @param out_data    receives malloc'd, uncompressed data (caller frees)
 * @param out_len     receives data length in bytes
 * @return true on success
 */
bool precir_image_load_bmp(
    Storage* storage,
    const char* path,
    uint16_t target_w,
    uint16_t target_h,
    PrecIRColorMode color_mode,
    uint8_t** out_data,
    size_t* out_len);

/** Load a BMP and choose raw or RLE encoding, padded to a 20-byte frame boundary. */
bool precir_image_load_bmp_payload(
    Storage* storage,
    const char* path,
    uint16_t target_w,
    uint16_t target_h,
    PrecIRColorMode color_mode,
    PrecIRImagePayload* out_payload);

/** Build a compressed all-white image for Test Clear / Clear With Saved. */
bool precir_image_make_white_payload(
    uint16_t width,
    uint16_t height,
    PrecIRColorMode color_mode,
    PrecIRImagePayload* out_payload);

/** Free and reset a prepared image payload. */
void precir_image_payload_free(PrecIRImagePayload* payload);

/** RLE-compress a complete image bitstream using PrecIR's gamma-coded runs.
 *
 * @return true only when the encoded bitstream is smaller than the raw one.
 *         On false, no output buffer is returned.
 */
bool precir_image_rle_compress(
    const uint8_t* raw,
    size_t raw_len,
    uint8_t** out_data,
    size_t* out_len);

/** Check the RLE encoder against the reference all-white/all-black vectors. */
bool precir_image_self_test(void);

/** Pad data with zero bytes to a 20-byte data-frame boundary.
 *
 * The original allocation remains valid and unchanged if reallocation fails.
 */
bool precir_image_pad(uint8_t** data, size_t* len);
