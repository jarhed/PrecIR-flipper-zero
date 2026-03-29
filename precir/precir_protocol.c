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
static size_t frame_raw_header(uint8_t* buf, uint8_t protocol, const uint8_t plid[4], uint8_t cmd) {
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

bool precir_plid_from_barcode(const char* barcode, uint8_t plid[4]) {
    if(!barcode) return false;

    /* Must be exactly 17 characters. */
    size_t len = 0;
    for(const char* p = barcode; *p; p++) len++;
    if(len != 17) return false;

    /* Parse two 5-digit groups from barcode[2..6] and barcode[7..11]. */
    uint32_t lo = 0;
    for(int i = 2; i < 7; i++) {
        char c = barcode[i];
        if(c < '0' || c > '9') return false;
        lo = lo * 10 + (uint32_t)(c - '0');
    }

    uint32_t hi = 0;
    for(int i = 7; i < 12; i++) {
        char c = barcode[i];
        if(c < '0' || c > '9') return false;
        hi = hi * 10 + (uint32_t)(c - '0');
    }

    uint32_t id_value = lo + (hi << 16);

    /* Match Python's get_plid byte ordering:
     *   PLID[0] = (id_value >> 8) & 0xFF
     *   PLID[1] = id_value & 0xFF
     *   PLID[2] = (id_value >> 24) & 0xFF
     *   PLID[3] = (id_value >> 16) & 0xFF */
    plid[0] = (uint8_t)((id_value >> 8) & 0xFF);
    plid[1] = (uint8_t)(id_value & 0xFF);
    plid[2] = (uint8_t)((id_value >> 24) & 0xFF);
    plid[3] = (uint8_t)((id_value >> 16) & 0xFF);

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
