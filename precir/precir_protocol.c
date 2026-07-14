#include "precir_protocol.h"
#include <string.h>

/* ---- Helpers ---- */

/** Place PLID into frame in wire order (reversed from array order). */
static void plid_to_wire(uint8_t* dst, const uint8_t plid[4]) {
    dst[0] = plid[3];
    dst[1] = plid[2];
    dst[2] = plid[1];
    dst[3] = plid[0];
}

/** Build the 6-byte "raw" frame header: [protocol][PLID wire order][cmd]. */
static size_t
    frame_raw_header(uint8_t* buf, uint8_t protocol, const uint8_t plid[4], uint8_t cmd) {
    buf[0] = protocol;
    plid_to_wire(&buf[1], plid);
    buf[5] = cmd;
    return 6;
}

/** Build the 10-byte "MCU" frame header used by DM commands:
 *  [0x85][PLID wire][0x34][0x00 0x00 0x00][sub-cmd]. */
static size_t frame_mcu_header(uint8_t* buf, const uint8_t plid[4], uint8_t cmd) {
    buf[0] = PRECIR_PROTO_DM;
    plid_to_wire(&buf[1], plid);
    buf[5] = 0x34;
    buf[6] = 0x00;
    buf[7] = 0x00;
    buf[8] = 0x00;
    buf[9] = cmd;
    return 10;
}

/** Append big-endian 16-bit value. */
static void append_word(uint8_t* buf, size_t* pos, uint16_t value) {
    buf[(*pos)++] = (uint8_t)(value >> 8);
    buf[(*pos)++] = (uint8_t)(value & 0xFF);
}

/** Append CRC-16 (little-endian) to buf at *pos, advance pos by 2. */
static void append_crc(uint8_t* buf, size_t* pos, size_t crc_start) {
    uint16_t crc = precir_crc16(&buf[crc_start], *pos - crc_start);
    buf[(*pos)++] = (uint8_t)(crc & 0xFF);
    buf[(*pos)++] = (uint8_t)((crc >> 8) & 0xFF);
}

/* ---- PLID ---- */

static uint32_t parse_decimal_field(const char* text, size_t start, size_t length) {
    uint32_t value = 0;
    for(size_t i = 0; i < length; i++) {
        value = value * 10U + (uint32_t)(text[start + i] - '0');
    }
    return value;
}

PrecIRBarcodeValidation precir_barcode_validate(const char* barcode) {
    if(!barcode) return PrecIRBarcodeInvalidLength;

    size_t length = 0;
    while(length <= 17U && barcode[length] != '\0') {
        length++;
    }
    if(length != 17U || barcode[17] != '\0') return PrecIRBarcodeInvalidLength;

    if(barcode[0] < 'A' || barcode[0] > 'Z') return PrecIRBarcodeInvalidFormat;
    if(barcode[1] != '4') return PrecIRBarcodeInvalidFamily;

    for(size_t i = 2; i < 17U; i++) {
        if(barcode[i] < '0' || barcode[i] > '9') return PrecIRBarcodeInvalidFormat;
    }

    const uint32_t first_address = parse_decimal_field(barcode, 2, 5);
    const uint32_t second_address = parse_decimal_field(barcode, 7, 5);
    if(first_address > UINT16_MAX || second_address > UINT16_MAX) {
        return PrecIRBarcodeAddressOutOfRange;
    }

    uint32_t checksum = 0;
    for(size_t i = 0; i < 16U; i++) {
        checksum += (uint8_t)barcode[i];
    }
    if((checksum % 10U) != (uint32_t)(barcode[16] - '0')) {
        return PrecIRBarcodeInvalidChecksum;
    }

    return PrecIRBarcodeValid;
}

bool precir_plid_from_barcode(const char* barcode, uint8_t plid[4]) {
    if(!plid || precir_barcode_validate(barcode) != PrecIRBarcodeValid) return false;

    const uint32_t lo = parse_decimal_field(barcode, 2, 5);
    const uint32_t hi = parse_decimal_field(barcode, 7, 5);
    const uint32_t id_value = lo + (hi << 16);
    uint8_t result[4];

    /* Match Python's get_plid byte ordering:
     *   PLID[0] = (id_value >> 8) & 0xFF
     *   PLID[1] = id_value & 0xFF
     *   PLID[2] = (id_value >> 24) & 0xFF
     *   PLID[3] = (id_value >> 16) & 0xFF */
    result[0] = (uint8_t)((id_value >> 8) & 0xFF);
    result[1] = (uint8_t)(id_value & 0xFF);
    result[2] = (uint8_t)((id_value >> 24) & 0xFF);
    result[3] = (uint8_t)((id_value >> 16) & 0xFF);

    memcpy(plid, result, sizeof(result));

    return true;
}

/* ---- CRC-16 ---- */

uint16_t precir_crc16(const uint8_t* data, size_t len) {
    uint16_t result = PRECIR_CRC_INIT;

    for(size_t i = 0; i < len; i++) {
        result ^= data[i];
        for(int bi = 0; bi < 8; bi++) {
            if(result & 1) {
                result >>= 1;
                result ^= PRECIR_CRC_POLY;
            } else {
                result >>= 1;
            }
        }
    }

    return result;
}

bool precir_protocol_self_test(void) {
    static const uint8_t crc_input[] = {
        0x84,
        0x00,
        0x00,
        0x00,
        0x00,
        0xAB,
        0x11,
        0x00,
        0x00,
    };
    static const uint8_t expected_plid[] = {0xE7, 0x01, 0x45, 0x63};
    static const uint8_t expected_wake[] = {
        0x85, 0x63, 0x45, 0x01, 0xE7, 0x17, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x8E, 0x61,
    };
    static const uint8_t expected_refresh[] = {
        0x85, 0x63, 0x45, 0x01, 0xE7, 0x34, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0xA3,
    };
    static const uint8_t expected_params[] = {
        0x85, 0x63, 0x45, 0x01, 0xE7, 0x34, 0x00, 0x00, 0x00, 0x05, 0x00, 0x28,
        0x00, 0x02, 0x03, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0xA3,
    };
    uint8_t plid[4];
    uint8_t frame[PRECIR_FRAME_MAX];
    size_t frame_len;

    if(precir_crc16(crc_input, sizeof(crc_input)) != 0xE4A5) return false;
    if(!precir_plid_from_barcode("G4591371776312423", plid)) return false;
    if(memcmp(plid, expected_plid, sizeof(expected_plid)) != 0) return false;

    frame_len = precir_build_wake_frame(frame, plid);
    if(frame_len != sizeof(expected_wake) ||
       memcmp(frame, expected_wake, sizeof(expected_wake)) != 0) {
        return false;
    }

    frame_len = precir_build_refresh_frame(frame, plid);
    if(frame_len != sizeof(expected_refresh) ||
       memcmp(frame, expected_refresh, sizeof(expected_refresh)) != 0) {
        return false;
    }

    frame_len = precir_build_params_frame(frame, plid, 40, 2, 3, 8, 1);
    if(frame_len != sizeof(expected_params) ||
       memcmp(frame, expected_params, sizeof(expected_params)) != 0) {
        return false;
    }

    return true;
}

/* ---- Frame building ---- */

size_t precir_build_wake_frame(uint8_t* buf, const uint8_t plid[4]) {
    /* raw_frame(0x85, PLID, 0x17) */
    size_t pos = frame_raw_header(buf, PRECIR_PROTO_DM, plid, PRECIR_CMD_WAKE);

    /* 0x01, 0x00, 0x00, 0x00 */
    buf[pos++] = 0x01;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    /* 22 bytes of 0x01 */
    for(int i = 0; i < 22; i++) {
        buf[pos++] = 0x01;
    }

    /* CRC over entire frame so far */
    append_crc(buf, &pos, 0);

    return pos;
}

size_t precir_build_params_frame(
    uint8_t* buf,
    const uint8_t plid[4],
    uint16_t total_bytes,
    uint8_t compression,
    uint8_t page,
    uint16_t width,
    uint16_t height) {
    size_t pos = frame_mcu_header(buf, plid, PRECIR_CMD_PARAMS);

    /* Total byte count (big-endian) */
    append_word(buf, &pos, total_bytes);
    /* Unused byte */
    buf[pos++] = 0x00;
    /* Compression type */
    buf[pos++] = compression;
    /* Page number */
    buf[pos++] = page;
    /* Width, height (big-endian) */
    append_word(buf, &pos, width);
    append_word(buf, &pos, height);
    /* Position x, y = 0, 0 */
    append_word(buf, &pos, 0x0000);
    append_word(buf, &pos, 0x0000);
    /* Keycode */
    append_word(buf, &pos, 0x0000);
    /* Flags: 0x80 = update, 0x08 = set base page */
    buf[pos++] = 0x88;
    /* Enabled pages bitmap */
    append_word(buf, &pos, 0x0000);
    /* 4 trailing zeroes */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    /* CRC over entire frame */
    append_crc(buf, &pos, 0);

    return pos;
}

size_t precir_build_data_frame(
    uint8_t* buf,
    const uint8_t plid[4],
    uint16_t index,
    const uint8_t* data,
    size_t data_len) {
    size_t pos = frame_mcu_header(buf, plid, PRECIR_CMD_DATA);

    /* Frame index (big-endian) */
    append_word(buf, &pos, index);

    /* 20 data bytes, zero-padded if data_len < 20 */
    for(size_t i = 0; i < PRECIR_DATA_PER_FRAME; i++) {
        buf[pos++] = (i < data_len) ? data[i] : 0x00;
    }

    /* CRC over entire frame */
    append_crc(buf, &pos, 0);

    return pos;
}

size_t precir_build_refresh_frame(uint8_t* buf, const uint8_t plid[4]) {
    size_t pos = frame_mcu_header(buf, plid, PRECIR_CMD_REFRESH);

    /* 22 bytes of 0x00 */
    for(int i = 0; i < 22; i++) {
        buf[pos++] = 0x00;
    }

    /* CRC over entire frame */
    append_crc(buf, &pos, 0);

    return pos;
}

size_t precir_build_segment_frame(
    uint8_t* buf,
    const uint8_t plid[4],
    const uint8_t bitmap[PRECIR_SEGMENT_BITMAP]) {
    /* raw_frame(0x84, PLID, 0xBA) */
    size_t pos = frame_raw_header(buf, PRECIR_PROTO_SEGMENT, plid, PRECIR_CMD_SEGMENT);

    /* 3 zero bytes */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    /* 23-byte segment bitmap */
    memcpy(&buf[pos], bitmap, PRECIR_SEGMENT_BITMAP);
    pos += PRECIR_SEGMENT_BITMAP;

    /* Segment bitmap CRC (over just the 23-byte bitmap) */
    uint16_t seg_crc = precir_crc16(bitmap, PRECIR_SEGMENT_BITMAP);
    buf[pos++] = (uint8_t)(seg_crc & 0xFF);
    buf[pos++] = (uint8_t)((seg_crc >> 8) & 0xFF);

    /* Trailing metadata: page number, duration, etc. */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x09;
    buf[pos++] = 0x00;
    buf[pos++] = 0x10;
    buf[pos++] = 0x00;
    buf[pos++] = 0x31;

    /* CRC over entire frame (from byte 0 up to here) */
    append_crc(buf, &pos, 0);

    return pos;
}
