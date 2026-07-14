#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- Carrier & timing constants ---- */

#define PRECIR_CARRIER_FREQ 1250000 /* Protocol target: 1.25 MHz */
#define PRECIR_DUTY_CYCLE   0.50f

/* PP4 timing (microseconds) */
#define PRECIR_PP4_BURST_US 40
/* Quiet gap after each burst for 2-bit values 0..3. */
#define PRECIR_PP4_SYM0_US  61
#define PRECIR_PP4_SYM1_US  244
#define PRECIR_PP4_SYM2_US  122
#define PRECIR_PP4_SYM3_US  183

/* PP16 timing (microseconds) */
#define PRECIR_PP16_BURST_US 21
/* PP16 gaps use a non-linear lookup table in precir_ir.c. */

/* Protocol bytes */
#define PRECIR_PROTO_SEGMENT 0x84
#define PRECIR_PROTO_DM      0x85

/* Commands */
#define PRECIR_CMD_WAKE    0x17
#define PRECIR_CMD_PARAMS  0x05
#define PRECIR_CMD_DATA    0x20
#define PRECIR_CMD_REFRESH 0x01
#define PRECIR_CMD_SEGMENT 0xBA

/* Frame constants */
#define PRECIR_DATA_PER_FRAME  20
#define PRECIR_SEGMENT_BITMAP  23
#define PRECIR_WAKE_REPEATS    400
#define PRECIR_PP16_HEADER_LEN 4

/* CRC init / polynomial (CRC-16 reversed) */
#define PRECIR_CRC_INIT 0x8408
#define PRECIR_CRC_POLY 0x8408

/* ---- Enumerations ---- */

typedef enum {
    PrecIRDisplayTypeSegment,
    PrecIRDisplayTypeDM,
} PrecIRDisplayType;

typedef enum {
    PrecIRDisplaySizeMedium, /* 208 x 112 */
    PrecIRDisplaySizeLarge, /* 296 x 128 */
} PrecIRDisplaySize;

typedef enum {
    PrecIRColorModeBW,
    PrecIRColorModeBWR, /* black/white/red */
    PrecIRColorMode4C, /* 4-color */
} PrecIRColorMode;

typedef enum {
    PrecIRProtocolPP4,
    PrecIRProtocolPP16,
} PrecIRProtocolMode;

typedef enum {
    PrecIRBarcodeValid,
    PrecIRBarcodeInvalidLength,
    PrecIRBarcodeInvalidFormat,
    PrecIRBarcodeInvalidFamily,
    PrecIRBarcodeAddressOutOfRange,
    PrecIRBarcodeInvalidChecksum,
} PrecIRBarcodeValidation;

/* ---- Display dimensions helper ---- */

static inline uint16_t precir_display_width(PrecIRDisplaySize size) {
    return (size == PrecIRDisplaySizeMedium) ? 208 : 296;
}

static inline uint16_t precir_display_height(PrecIRDisplaySize size) {
    return (size == PrecIRDisplaySizeMedium) ? 112 : 128;
}

/* ---- PLID ---- */

/** Validate a 17-character ESL barcode without reading past its terminator. */
PrecIRBarcodeValidation precir_barcode_validate(const char* barcode);

/** Parse a validated 17-character barcode string into a 4-byte PLID.
 *  Returns true on success and leaves plid untouched on failure. */
bool precir_plid_from_barcode(const char* barcode, uint8_t plid[4]);

/** Run protocol golden-vector checks. */
bool precir_protocol_self_test(void);

/* ---- CRC ---- */

uint16_t precir_crc16(const uint8_t* data, size_t len);

/* ---- Frame building ---- */

/** Maximum frame buffer size (generous upper bound). */
#define PRECIR_FRAME_MAX 64

/** Build a wake-up frame.  Returns frame length. */
size_t precir_build_wake_frame(uint8_t* buf, const uint8_t plid[4]);

/** Build the image-parameters frame.  Returns frame length.
 *  @param total_bytes  padded image data size in bytes
 *  @param compression  0 = raw, 2 = RLE
 *  @param page         page number (0-15) */
size_t precir_build_params_frame(
    uint8_t* buf,
    const uint8_t plid[4],
    uint16_t total_bytes,
    uint8_t compression,
    uint8_t page,
    uint16_t width,
    uint16_t height);

/** Build one image-data frame.
 *  @param index   data frame index (0-based)
 *  @param data    pointer to 20-byte chunk
 *  @param data_len  actual bytes (<=20, padded with 0) */
size_t precir_build_data_frame(
    uint8_t* buf,
    const uint8_t plid[4],
    uint16_t index,
    const uint8_t* data,
    size_t data_len);

/** Build a display-refresh frame.  Returns frame length. */
size_t precir_build_refresh_frame(uint8_t* buf, const uint8_t plid[4]);

/** Build a segment-update frame.
 *  @param bitmap  23-byte segment bitmap */
size_t precir_build_segment_frame(
    uint8_t* buf,
    const uint8_t plid[4],
    const uint8_t bitmap[PRECIR_SEGMENT_BITMAP]);
