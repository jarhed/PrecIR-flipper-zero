#include "precir_image.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define BMP_HEADER_READ_SIZE 54U
#define BMP_FILE_HEADER_SIZE 14U
#define BMP_MIN_DIB_SIZE     40U
#define BMP_CACHE_SIZE       516U

#define BMP_FILE_SIZE_OFFSET   2U
#define BMP_DATA_OFFSET        10U
#define BMP_DIB_SIZE_OFFSET    14U
#define BMP_WIDTH_OFFSET       18U
#define BMP_HEIGHT_OFFSET      22U
#define BMP_PLANES_OFFSET      26U
#define BMP_BPP_OFFSET         28U
#define BMP_COMPRESSION_OFFSET 30U

typedef struct {
    File* file;
    uint32_t data_offset;
    uint32_t width;
    uint32_t height;
    uint32_t row_stride;
    uint8_t bytes_per_pixel;
    bool top_down;

    uint8_t cache[BMP_CACHE_SIZE];
    uint32_t cache_offset;
    size_t cache_len;
    bool cache_valid;
} PrecIRBmpReader;

static uint16_t read_u16_le(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t read_i32_le(const uint8_t* p) {
    return (int32_t)read_u32_le(p);
}

static void bmp_reader_close(PrecIRBmpReader* reader) {
    if(reader->file) {
        storage_file_close(reader->file);
        storage_file_free(reader->file);
        reader->file = NULL;
    }
}

static bool bmp_reader_open(PrecIRBmpReader* reader, Storage* storage, const char* path) {
    memset(reader, 0, sizeof(*reader));
    if(!storage || !path) return false;

    reader->file = storage_file_alloc(storage);
    if(!reader->file) return false;

    if(!storage_file_open(reader->file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(reader->file);
        reader->file = NULL;
        return false;
    }

    const uint64_t actual_file_size = storage_file_size(reader->file);
    if(actual_file_size < BMP_HEADER_READ_SIZE) {
        bmp_reader_close(reader);
        return false;
    }

    uint8_t header[BMP_HEADER_READ_SIZE];
    if(storage_file_read(reader->file, header, sizeof(header)) != sizeof(header)) {
        bmp_reader_close(reader);
        return false;
    }

    const uint32_t declared_file_size = read_u32_le(&header[BMP_FILE_SIZE_OFFSET]);
    const uint32_t data_offset = read_u32_le(&header[BMP_DATA_OFFSET]);
    const uint32_t dib_size = read_u32_le(&header[BMP_DIB_SIZE_OFFSET]);
    const int32_t signed_width = read_i32_le(&header[BMP_WIDTH_OFFSET]);
    const int32_t signed_height = read_i32_le(&header[BMP_HEIGHT_OFFSET]);
    const uint16_t planes = read_u16_le(&header[BMP_PLANES_OFFSET]);
    const uint16_t bpp = read_u16_le(&header[BMP_BPP_OFFSET]);
    const uint32_t compression = read_u32_le(&header[BMP_COMPRESSION_OFFSET]);

    if(header[0] != 'B' || header[1] != 'M' || dib_size < BMP_MIN_DIB_SIZE || signed_width <= 0 ||
       signed_height == 0 || signed_height == INT32_MIN || planes != 1 ||
       (bpp != 24 && bpp != 32) || compression != 0) {
        bmp_reader_close(reader);
        return false;
    }

    const uint32_t width = (uint32_t)signed_width;
    const uint32_t height = (signed_height < 0) ? (uint32_t)(-signed_height) :
                                                  (uint32_t)signed_height;
    const uint32_t bytes_per_pixel = bpp / 8U;
    const uint64_t unpadded_row_size = (uint64_t)width * bytes_per_pixel;
    const uint64_t row_stride = (unpadded_row_size + 3U) & ~UINT64_C(3);
    const uint64_t pixel_end = (uint64_t)data_offset + row_stride * height;
    const uint64_t minimum_data_offset = (uint64_t)BMP_FILE_HEADER_SIZE + dib_size;

    /* Flipper's storage seek offset is uint32_t, so files beyond this boundary
     cannot be addressed safely even if their headers are otherwise valid. */
    if(data_offset < minimum_data_offset || row_stride > UINT32_MAX ||
       pixel_end > actual_file_size || pixel_end > UINT32_MAX ||
       (declared_file_size != 0 &&
        (declared_file_size < pixel_end || declared_file_size > actual_file_size))) {
        bmp_reader_close(reader);
        return false;
    }

    reader->data_offset = data_offset;
    reader->width = width;
    reader->height = height;
    reader->row_stride = (uint32_t)row_stride;
    reader->bytes_per_pixel = (uint8_t)bytes_per_pixel;
    reader->top_down = signed_height < 0;
    return true;
}

static bool bmp_reader_get_rgb(
    PrecIRBmpReader* reader,
    uint32_t x,
    uint32_t y,
    uint8_t* red,
    uint8_t* green,
    uint8_t* blue) {
    if(x >= reader->width || y >= reader->height) return false;

    const uint32_t file_row = reader->top_down ? y : (reader->height - 1U - y);
    const uint64_t row_start =
        (uint64_t)reader->data_offset + (uint64_t)file_row * reader->row_stride;
    const uint64_t pixel_offset = row_start + (uint64_t)x * reader->bytes_per_pixel;
    const uint64_t pixel_end = pixel_offset + reader->bytes_per_pixel;
    const uint64_t cache_end = (uint64_t)reader->cache_offset + reader->cache_len;

    if(!reader->cache_valid || pixel_offset < reader->cache_offset || pixel_end > cache_end) {
        const uint64_t row_pixel_end =
            row_start + (uint64_t)reader->width * reader->bytes_per_pixel;
        const uint64_t bytes_left = row_pixel_end - pixel_offset;
        const size_t read_size = (bytes_left < sizeof(reader->cache)) ? (size_t)bytes_left :
                                                                        sizeof(reader->cache);

        if(read_size < reader->bytes_per_pixel ||
           !storage_file_seek(reader->file, (uint32_t)pixel_offset, true) ||
           storage_file_read(reader->file, reader->cache, read_size) != read_size) {
            reader->cache_valid = false;
            return false;
        }

        reader->cache_offset = (uint32_t)pixel_offset;
        reader->cache_len = read_size;
        reader->cache_valid = true;
    }

    const uint8_t* bgr = reader->cache + (size_t)(pixel_offset - reader->cache_offset);
    *blue = bgr[0];
    *green = bgr[1];
    *red = bgr[2];
    return true;
}

static void image_set_bit(uint8_t* data, size_t bit_index, bool value) {
    if(value) data[bit_index / 8U] |= (uint8_t)(0x80U >> (bit_index % 8U));
}

static bool image_luma_is_white(uint8_t red, uint8_t green, uint8_t blue) {
    /* Same 0.21/0.72/0.07 threshold as the reference converter, expressed
     with integers to avoid floating-point work on the Flipper. */
    const uint32_t weighted = 21U * red + 72U * green + 7U * blue;
    return weighted >= 12750U;
}

bool precir_image_load_bmp(
    Storage* storage,
    const char* path,
    uint16_t target_w,
    uint16_t target_h,
    PrecIRColorMode color_mode,
    uint8_t** out_data,
    size_t* out_len) {
    if(out_data) *out_data = NULL;
    if(out_len) *out_len = 0;
    if(!out_data || !out_len || target_w == 0 || target_h == 0 ||
       (color_mode != PrecIRColorModeBW && color_mode != PrecIRColorModeBWR &&
        color_mode != PrecIRColorMode4C)) {
        return false;
    }

    if((size_t)target_w > SIZE_MAX / (size_t)target_h) return false;
    const size_t plane_bits = (size_t)target_w * target_h;

    /* PrecIR transmits complete bytes and starts the color plane immediately
     after the BW plane. Supported ESL sizes are byte aligned. */
    if((plane_bits & 7U) != 0) return false;

    const size_t plane_bytes = plane_bits / 8U;
    const size_t plane_count = (color_mode == PrecIRColorModeBW) ? 1U : 2U;
    if(plane_bytes > SIZE_MAX / plane_count) return false;
    const size_t data_len = plane_bytes * plane_count;

    PrecIRBmpReader reader;
    if(!bmp_reader_open(&reader, storage, path)) return false;

    uint8_t* data = calloc(data_len, 1U);
    if(!data) {
        bmp_reader_close(&reader);
        return false;
    }

    uint32_t crop_x;
    uint32_t crop_y;
    uint32_t crop_w;
    uint32_t crop_h;

    if((uint64_t)reader.width * target_h > (uint64_t)reader.height * target_w) {
        crop_h = reader.height;
        crop_w = (uint32_t)((uint64_t)reader.height * target_w / target_h);
        if(crop_w == 0) crop_w = 1;
        crop_x = (reader.width - crop_w) / 2U;
        crop_y = 0;
    } else {
        crop_w = reader.width;
        crop_h = (uint32_t)((uint64_t)reader.width * target_h / target_w);
        if(crop_h == 0) crop_h = 1;
        crop_x = 0;
        crop_y = (reader.height - crop_h) / 2U;
    }

    bool ok = true;
    for(uint32_t target_y = 0; ok && target_y < target_h; target_y++) {
        uint32_t source_y = crop_y + (uint32_t)((uint64_t)target_y * crop_h / target_h);
        if(source_y >= reader.height) source_y = reader.height - 1U;

        for(uint32_t target_x = 0; target_x < target_w; target_x++) {
            uint32_t source_x = crop_x + (uint32_t)((uint64_t)target_x * crop_w / target_w);
            if(source_x >= reader.width) source_x = reader.width - 1U;

            uint8_t red;
            uint8_t green;
            uint8_t blue;
            if(!bmp_reader_get_rgb(&reader, source_x, source_y, &red, &green, &blue)) {
                ok = false;
                break;
            }

            const bool is_red = red >= 128U && green < 128U && blue < 128U;
            const bool is_yellow = red >= 128U && green >= 128U && blue < 128U;
            const bool luma_white = image_luma_is_white(red, green, blue);
            bool bw_bit = luma_white;
            bool color_bit = true;

            if(color_mode == PrecIRColorModeBWR) {
                /* Match the reference PrecIR converter: red is 10 and a
                 * yellow-accent source pixel is 00. The second plane selects
                 * the label's accent pigment. */
                if(is_red) {
                    bw_bit = true;
                    color_bit = false;
                } else if(is_yellow) {
                    bw_bit = false;
                    color_bit = false;
                }
            } else if(color_mode == PrecIRColorMode4C) {
                /* Four-color extension: red=10, yellow=00, black=01,
                 * white=11. The second plane selects color when clear. */
                if(is_red) {
                    bw_bit = true;
                    color_bit = false;
                } else if(is_yellow) {
                    bw_bit = false;
                    color_bit = false;
                }
            }

            const size_t pixel_index = (size_t)target_y * target_w + target_x;
            image_set_bit(data, pixel_index, bw_bit);
            if(plane_count == 2U) {
                image_set_bit(data, plane_bits + pixel_index, color_bit);
            }
        }
    }

    bmp_reader_close(&reader);
    if(!ok) {
        free(data);
        return false;
    }

    *out_data = data;
    *out_len = data_len;
    return true;
}

static uint8_t image_get_bit(const uint8_t* data, size_t bit_index) {
    return (uint8_t)((data[bit_index / 8U] >> (7U - (bit_index % 8U))) & 1U);
}

static void image_write_one(uint8_t* data, size_t bit_index) {
    data[bit_index / 8U] |= (uint8_t)(0x80U >> (bit_index % 8U));
}

static bool rle_append_run(
    uint8_t* encoded,
    size_t capacity_bits,
    size_t* encoded_bits,
    size_t run_length) {
    size_t significant_bits = 0;
    for(size_t value = run_length; value != 0; value >>= 1U)
        significant_bits++;

    const size_t code_bits = significant_bits * 2U - 1U;
    if(*encoded_bits > capacity_bits || code_bits > capacity_bits - *encoded_bits) return false;

    *encoded_bits += significant_bits - 1U;
    for(size_t bit = significant_bits; bit > 0; bit--) {
        if((run_length >> (bit - 1U)) & 1U) image_write_one(encoded, *encoded_bits);
        (*encoded_bits)++;
    }
    return true;
}

static bool precir_image_rle_encode_bounded(
    const uint8_t* raw,
    size_t raw_len,
    uint8_t** out_encoded,
    size_t* out_encoded_bits) {
    *out_encoded = NULL;
    *out_encoded_bits = 0;

    const size_t raw_bits = raw_len * 8U;
    uint8_t* encoded = calloc(raw_len, 1U);
    if(!encoded) return false;

    const uint8_t first_pixel = image_get_bit(raw, 0);
    if(first_pixel) image_write_one(encoded, 0);

    size_t encoded_bits = 1;
    uint8_t run_pixel = first_pixel;
    size_t run_length = 1;

    for(size_t bit_index = 1; bit_index < raw_bits; bit_index++) {
        const uint8_t pixel = image_get_bit(raw, bit_index);
        if(pixel == run_pixel) {
            run_length++;
        } else {
            if(!rle_append_run(encoded, raw_bits, &encoded_bits, run_length)) {
                free(encoded);
                return false;
            }
            run_pixel = pixel;
            run_length = 1;
        }
    }

    /* Match the reference decoder contract: a final one-pixel run is
     implied by the known image dimensions and is not emitted. */
    if(run_length > 1U && !rle_append_run(encoded, raw_bits, &encoded_bits, run_length)) {
        free(encoded);
        return false;
    }

    *out_encoded = encoded;
    *out_encoded_bits = encoded_bits;
    return true;
}

bool precir_image_rle_compress(
    const uint8_t* raw,
    size_t raw_len,
    uint8_t** out_data,
    size_t* out_len) {
    if(out_data) *out_data = NULL;
    if(out_len) *out_len = 0;
    if(!raw || raw_len == 0 || !out_data || !out_len || raw_len > SIZE_MAX / 8U) return false;

    uint8_t* encoded;
    size_t encoded_bits;
    if(!precir_image_rle_encode_bounded(raw, raw_len, &encoded, &encoded_bits)) return false;

    const size_t raw_bits = raw_len * 8U;
    if(encoded_bits >= raw_bits) {
        free(encoded);
        return false;
    }

    const size_t encoded_bytes = (encoded_bits + 7U) / 8U;
    uint8_t* trimmed = realloc(encoded, encoded_bytes);
    if(trimmed) encoded = trimmed;

    *out_data = encoded;
    *out_len = encoded_bytes;
    return true;
}

bool precir_image_self_test(void) {
    static const struct {
        uint8_t raw;
        uint8_t encoded;
    } vectors[] = {
        {.raw = 0xFF, .encoded = 0x88},
        {.raw = 0x00, .encoded = 0x08},
    };

    for(size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        uint8_t* encoded;
        size_t encoded_bits;
        if(!precir_image_rle_encode_bounded(&vectors[i].raw, 1, &encoded, &encoded_bits)) {
            return false;
        }

        const bool matches = encoded_bits == 8U && encoded[0] == vectors[i].encoded;
        free(encoded);
        if(!matches) return false;
    }

    static const struct {
        uint16_t width;
        uint16_t height;
        PrecIRColorMode color_mode;
        uint8_t prefix[4];
    } white_vectors[] = {
        {208U, 112U, PrecIRColorModeBW, {0x80U, 0x01U, 0x6CU, 0x00U}},
        {208U, 112U, PrecIRColorModeBWR, {0x80U, 0x00U, 0xB6U, 0x00U}},
        {296U, 128U, PrecIRColorModeBW, {0x80U, 0x00U, 0x94U, 0x00U}},
        {296U, 128U, PrecIRColorModeBWR, {0x80U, 0x00U, 0x4AU, 0x00U}},
    };

    for(size_t i = 0; i < sizeof(white_vectors) / sizeof(white_vectors[0]); i++) {
        PrecIRImagePayload payload;
        if(!precir_image_make_white_payload(
               white_vectors[i].width,
               white_vectors[i].height,
               white_vectors[i].color_mode,
               &payload)) {
            return false;
        }

        bool matches = payload.data_len == PRECIR_DATA_PER_FRAME && payload.compression == 2U &&
                       memcmp(payload.data, white_vectors[i].prefix, 4U) == 0;
        for(size_t byte = 4U; matches && byte < payload.data_len; byte++) {
            matches = payload.data[byte] == 0U;
        }
        precir_image_payload_free(&payload);
        if(!matches) return false;
    }

    return true;
}

bool precir_image_pad(uint8_t** data, size_t* len) {
    if(!data || !*data || !len || *len == 0) return false;

    const size_t remainder = *len % PRECIR_DATA_PER_FRAME;
    if(remainder == 0) return true;

    const size_t padding = PRECIR_DATA_PER_FRAME - remainder;
    if(*len > SIZE_MAX - padding) return false;

    const size_t padded_len = *len + padding;
    uint8_t* padded = realloc(*data, padded_len);
    if(!padded) return false;

    memset(padded + *len, 0, padding);
    *data = padded;
    *len = padded_len;
    return true;
}

void precir_image_payload_free(PrecIRImagePayload* payload) {
    if(!payload) return;

    free(payload->data);
    payload->data = NULL;
    payload->data_len = 0U;
    payload->compression = 0U;
}

bool precir_image_load_bmp_payload(
    Storage* storage,
    const char* path,
    uint16_t target_w,
    uint16_t target_h,
    PrecIRColorMode color_mode,
    PrecIRImagePayload* out_payload) {
    if(!out_payload) return false;
    out_payload->data = NULL;
    out_payload->data_len = 0U;
    out_payload->compression = 0U;

    uint8_t* raw_data = NULL;
    size_t raw_len = 0U;
    if(!precir_image_load_bmp(storage, path, target_w, target_h, color_mode, &raw_data, &raw_len)) {
        return false;
    }

    uint8_t* payload_data = raw_data;
    size_t payload_len = raw_len;
    uint8_t compression = 0U;

    uint8_t* compressed_data = NULL;
    size_t compressed_len = 0U;
    if(precir_image_rle_compress(raw_data, raw_len, &compressed_data, &compressed_len)) {
        free(raw_data);
        payload_data = compressed_data;
        payload_len = compressed_len;
        compression = 2U;
    }

    if(!precir_image_pad(&payload_data, &payload_len)) {
        free(payload_data);
        return false;
    }

    out_payload->data = payload_data;
    out_payload->data_len = payload_len;
    out_payload->compression = compression;
    return true;
}

bool precir_image_make_white_payload(
    uint16_t width,
    uint16_t height,
    PrecIRColorMode color_mode,
    PrecIRImagePayload* out_payload) {
    if(!out_payload) return false;
    out_payload->data = NULL;
    out_payload->data_len = 0U;
    out_payload->compression = 0U;

    if(width == 0U || height == 0U ||
       (color_mode != PrecIRColorModeBW && color_mode != PrecIRColorModeBWR &&
        color_mode != PrecIRColorMode4C)) {
        return false;
    }

    const size_t plane_count = (color_mode == PrecIRColorModeBW) ? 1U : 2U;
    if((size_t)width > SIZE_MAX / (size_t)height) return false;
    const size_t plane_pixels = (size_t)width * (size_t)height;
    if(plane_pixels > SIZE_MAX / plane_count) return false;
    const size_t run_length = plane_pixels * plane_count;

    size_t significant_bits = 0U;
    for(size_t value = run_length; value != 0U; value >>= 1U) {
        significant_bits++;
    }
    if(significant_bits == 0U || significant_bits > (SIZE_MAX - 7U) / 2U) return false;

    /* RLE starts with the first pixel value, followed by the gamma-coded run.
     * One run spans both consecutive planes, exactly as the recovered v2.1 FAP. */
    const size_t encoded_bits = significant_bits * 2U;
    const size_t encoded_bytes = (encoded_bits + 7U) / 8U;
    uint8_t* encoded = calloc(encoded_bytes, 1U);
    if(!encoded) return false;

    image_write_one(encoded, 0U);
    size_t write_offset = 1U;
    if(!rle_append_run(encoded, encoded_bytes * 8U, &write_offset, run_length) ||
       write_offset != encoded_bits) {
        free(encoded);
        return false;
    }

    size_t payload_len = encoded_bytes;
    if(!precir_image_pad(&encoded, &payload_len)) {
        free(encoded);
        return false;
    }

    out_payload->data = encoded;
    out_payload->data_len = payload_len;
    out_payload->compression = 2U;
    return true;
}
