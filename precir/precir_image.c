#include "precir_image.h"
#include <stdlib.h>
#include <string.h>

/* BMP header offsets */
#define BMP_SIG_OFFSET       0
#define BMP_DATA_OFFSET      10
#define BMP_WIDTH_OFFSET     18
#define BMP_HEIGHT_OFFSET    22
#define BMP_BPP_OFFSET       28

#define BMP_HEADER_SIZE      14
#define BMP_INFO_HEADER_SIZE 40

/* Minimum BMP header we need to read */
#define BMP_HEADER_READ_SIZE (BMP_HEADER_SIZE + BMP_INFO_HEADER_SIZE)

static inline uint16_t read_u16_le(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static inline int32_t read_i32_le(const uint8_t* p) {
    return (int32_t)read_u32_le(p);
}

/**
 * Read full BMP pixel data into a top-down RGB buffer.
 * Returns malloc'd array of width*height*3 bytes (RGB), or NULL on failure.
 * Sets *out_w and *out_h to the image dimensions.
 */
static uint8_t* bmp_read_pixels(
    Storage* storage,
    const char* path,
    int32_t* out_w,
    int32_t* out_h) {
    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return NULL;
    }

    uint8_t hdr[BMP_HEADER_READ_SIZE];
    if(storage_file_read(file, hdr, BMP_HEADER_READ_SIZE) != BMP_HEADER_READ_SIZE) {
        storage_file_close(file);
        storage_file_free(file);
        return NULL;
    }

    /* Verify BMP signature */
    if(hdr[0] != 'B' || hdr[1] != 'M') {
        storage_file_close(file);
        storage_file_free(file);
        return NULL;
    }

    uint32_t data_offset = read_u32_le(&hdr[BMP_DATA_OFFSET]);
    int32_t width = read_i32_le(&hdr[BMP_WIDTH_OFFSET]);
    int32_t raw_height = read_i32_le(&hdr[BMP_HEIGHT_OFFSET]);
    uint16_t bpp = read_u16_le(&hdr[BMP_BPP_OFFSET]);

    if(width <= 0 || (bpp != 24 && bpp != 32)) {
        storage_file_close(file);
        storage_file_free(file);
        return NULL;
    }

    bool top_down = (raw_height < 0);
    int32_t height = top_down ? -raw_height : raw_height;

    if(height <= 0) {
        storage_file_close(file);
        storage_file_free(file);
        return NULL;
    }

    uint32_t bytes_per_pixel = bpp / 8;
    /* BMP rows are padded to 4-byte boundaries */
    uint32_t row_stride = ((uint32_t)width * bytes_per_pixel + 3) & ~3u;

    uint8_t* row_buf = malloc(row_stride);
    uint8_t* pixels = malloc((size_t)width * (size_t)height * 3);
    if(!row_buf || !pixels) {
        free(row_buf);
        free(pixels);
        storage_file_close(file);
        storage_file_free(file);
        return NULL;
    }

    /* Seek to pixel data */
    storage_file_seek(file, data_offset, true);

    for(int32_t row = 0; row < height; row++) {
        if(storage_file_read(file, row_buf, row_stride) != row_stride) {
            free(row_buf);
            free(pixels);
            storage_file_close(file);
            storage_file_free(file);
            return NULL;
        }

        /* Determine destination row: BMP bottom-up means first row in file
           is the bottom row of the image. We store top-down. */
        int32_t dest_row = top_down ? row : (height - 1 - row);
        uint8_t* dest = pixels + (size_t)dest_row * (size_t)width * 3;

        for(int32_t x = 0; x < width; x++) {
            uint8_t* src = row_buf + (size_t)x * bytes_per_pixel;
            /* BMP stores BGR(A), convert to RGB */
            dest[x * 3 + 0] = src[2]; /* R */
            dest[x * 3 + 1] = src[1]; /* G */
            dest[x * 3 + 2] = src[0]; /* B */
        }
    }

    free(row_buf);
    storage_file_close(file);
    storage_file_free(file);

    *out_w = width;
    *out_h = height;
    return pixels;
}

bool precir_image_load_bmp(
    Storage* storage,
    const char* path,
    uint16_t target_w,
    uint16_t target_h,
    PrecIRImageLayer layer,
    uint8_t** out_data,
    size_t* out_len) {
    int32_t bmp_w, bmp_h;
    uint8_t* pixels = bmp_read_pixels(storage, path, &bmp_w, &bmp_h);
    if(!pixels) return false;

    /* Center-crop and scale: pick the largest centered rectangle from the
       source that matches the target aspect ratio, then nearest-neighbor
       scale to target dimensions. */
    int32_t crop_x, crop_y, crop_w, crop_h;

    /* Determine crop region that matches target aspect ratio */
    if((int64_t)bmp_w * target_h > (int64_t)bmp_h * target_w) {
        /* Source is wider than target aspect: crop width */
        crop_h = bmp_h;
        crop_w = (int32_t)((int64_t)bmp_h * target_w / target_h);
        crop_x = (bmp_w - crop_w) / 2;
        crop_y = 0;
    } else {
        /* Source is taller than target aspect: crop height */
        crop_w = bmp_w;
        crop_h = (int32_t)((int64_t)bmp_w * target_h / target_w);
        crop_x = 0;
        crop_y = (bmp_h - crop_h) / 2;
    }

    /* Total pixels */
    size_t total_pixels = (size_t)target_w * (size_t)target_h;
    size_t out_bytes = (total_pixels + 7) / 8;

    uint8_t* data = malloc(out_bytes);
    if(!data) {
        free(pixels);
        return false;
    }
    memset(data, 0, out_bytes);

    size_t bit_idx = 0;
    for(int32_t ty = 0; ty < target_h; ty++) {
        /* Map target row to source row (nearest neighbor) */
        int32_t sy = crop_y + (int32_t)((int64_t)ty * crop_h / target_h);
        if(sy >= bmp_h) sy = bmp_h - 1;

        for(int32_t tx = 0; tx < target_w; tx++) {
            int32_t sx = crop_x + (int32_t)((int64_t)tx * crop_w / target_w);
            if(sx >= bmp_w) sx = bmp_w - 1;

            uint8_t* px = pixels + ((size_t)sy * (size_t)bmp_w + (size_t)sx) * 3;
            uint8_t r = px[0];
            uint8_t g = px[1];
            uint8_t b = px[2];

            uint8_t bit_val;

            if(layer == PrecIRImageLayerColor) {
                /* Color layer for BWR mode:
                   Red pixel (R>128, G<128, B<128):   BW=0, Color=0
                   Yellow pixel (R>128, G>128, B<128): BW=0, Color=0
                   White pixel (R>128, G>128, B>128):  BW=1, Color=1
                   Dark/black:                         BW=0, Color=1 */
                if(r > 128 && g < 128 && b < 128) {
                    /* Red -> color=0 */
                    bit_val = 0;
                } else if(r > 128 && g > 128 && b < 128) {
                    /* Yellow -> color=0 */
                    bit_val = 0;
                } else if(r > 128 && g > 128 && b > 128) {
                    /* White -> color=1 */
                    bit_val = 1;
                } else {
                    /* Dark/black -> color=1 */
                    bit_val = 1;
                }
            } else {
                /* BW layer */
                bool is_bwr_red = (r > 128 && g < 128 && b < 128);
                bool is_bwr_yellow = (r > 128 && g > 128 && b < 128);

                if(is_bwr_red) {
                    /* Red pixels are white in BW layer (so color layer shows through) */
                    bit_val = 1;
                } else if(is_bwr_yellow) {
                    /* Yellow pixels are black in BW layer */
                    bit_val = 0;
                } else {
                    /* Luma threshold */
                    float luma = 0.21f * r + 0.72f * g + 0.07f * b;
                    bit_val = (luma >= 128.0f) ? 1 : 0;
                }
            }

            /* Pack MSB first: bit 7 of first byte is the first pixel */
            if(bit_val) {
                data[bit_idx / 8] |= (uint8_t)(0x80 >> (bit_idx % 8));
            }
            bit_idx++;
        }
    }

    free(pixels);

    *out_data = data;
    *out_len = out_bytes;
    return true;
}

bool precir_image_rle_compress(
    const uint8_t* raw,
    size_t raw_len,
    uint8_t** out_data,
    size_t* out_len) {
    if(!raw || raw_len == 0) return false;

    size_t total_bits = raw_len * 8;

    /* Worst case: compressed could be much larger. Allocate generously
       and check at the end. We'll build a bit array first. */
    size_t max_comp_bits = total_bits * 2 + 64;
    uint8_t* comp_bits = malloc((max_comp_bits + 7) / 8);
    if(!comp_bits) return false;
    memset(comp_bits, 0, (max_comp_bits + 7) / 8);

    /* Helper to get bit from raw data (MSB first) */
    #define RAW_BIT(idx) (((raw[(idx) / 8]) >> (7 - ((idx) % 8))) & 1)

    /* Helper to set bit in comp_bits (MSB first) */
    #define SET_COMP_BIT(idx) (comp_bits[(idx) / 8] |= (uint8_t)(0x80 >> ((idx) % 8)))

    size_t comp_idx = 0;

    /* First bit: initial color */
    uint8_t first_bit = RAW_BIT(0);
    if(first_bit) {
        SET_COMP_BIT(comp_idx);
    }
    comp_idx++;

    /* Count runs and encode them */
    uint8_t run_pixel = first_bit;
    size_t run_count = 1;

    for(size_t i = 1; i < total_bits; i++) {
        uint8_t cur_bit = RAW_BIT(i);
        if(cur_bit == run_pixel) {
            run_count++;
        } else {
            /* Encode the run using unary coding of the binary representation:
               Convert run_count to binary, output (len-1) zeros, then the
               binary digits. */
            if(comp_idx + 64 >= max_comp_bits) {
                /* Grow buffer */
                max_comp_bits *= 2;
                uint8_t* new_buf = realloc(comp_bits, (max_comp_bits + 7) / 8);
                if(!new_buf) {
                    free(comp_bits);
                    return false;
                }
                comp_bits = new_buf;
                memset(comp_bits + ((max_comp_bits / 2) + 7) / 8, 0,
                       (max_comp_bits + 7) / 8 - ((max_comp_bits / 2) + 7) / 8);
            }

            /* Find number of bits in run_count */
            int num_bits = 0;
            size_t tmp = run_count;
            while(tmp) {
                num_bits++;
                tmp >>= 1;
            }

            /* Output (num_bits - 1) zeros */
            for(int z = 0; z < num_bits - 1; z++) {
                /* bit is already 0 */
                comp_idx++;
            }

            /* Output the binary representation of run_count */
            for(int b = num_bits - 1; b >= 0; b--) {
                if((run_count >> b) & 1) {
                    SET_COMP_BIT(comp_idx);
                }
                comp_idx++;
            }

            run_count = 1;
            run_pixel = cur_bit;
        }
    }

    /* Encode the last run */
    if(run_count > 1) {
        if(comp_idx + 64 >= max_comp_bits) {
            max_comp_bits = comp_idx + 128;
            uint8_t* new_buf = realloc(comp_bits, (max_comp_bits + 7) / 8);
            if(!new_buf) {
                free(comp_bits);
                return false;
            }
            comp_bits = new_buf;
        }

        int num_bits = 0;
        size_t tmp = run_count;
        while(tmp) {
            num_bits++;
            tmp >>= 1;
        }

        for(int z = 0; z < num_bits - 1; z++) {
            comp_idx++;
        }

        for(int b = num_bits - 1; b >= 0; b--) {
            if((run_count >> b) & 1) {
                SET_COMP_BIT(comp_idx);
            }
            comp_idx++;
        }
    }

    #undef RAW_BIT
    #undef SET_COMP_BIT

    /* Convert compressed bits to bytes (already packed MSB first) */
    size_t comp_bytes = (comp_idx + 7) / 8;

    /* Pad to multiple of PRECIR_DATA_PER_FRAME */
    size_t padded = comp_bytes;
    if(padded % PRECIR_DATA_PER_FRAME != 0) {
        padded += PRECIR_DATA_PER_FRAME - (padded % PRECIR_DATA_PER_FRAME);
    }

    /* Check if compression is beneficial */
    if(comp_bytes >= raw_len) {
        free(comp_bits);
        return false;
    }

    /* Trim/pad the output buffer */
    uint8_t* result = realloc(comp_bits, padded);
    if(!result) {
        free(comp_bits);
        return false;
    }

    /* Zero out any trailing bytes */
    if(padded > comp_bytes) {
        memset(result + comp_bytes, 0, padded - comp_bytes);
    }

    *out_data = result;
    *out_len = padded;
    return true;
}

void precir_image_pad(uint8_t** data, size_t* len) {
    if(!data || !*data || !len || *len == 0) return;

    size_t current = *len;
    size_t remainder = current % PRECIR_DATA_PER_FRAME;
    if(remainder == 0) return;

    size_t padded = current + (PRECIR_DATA_PER_FRAME - remainder);
    uint8_t* new_data = realloc(*data, padded);
    if(!new_data) return;

    memset(new_data + current, 0, padded - current);
    *data = new_data;
    *len = padded;
}

bool precir_image_has_color(Storage* storage, const char* path) {
    int32_t bmp_w, bmp_h;
    uint8_t* pixels = bmp_read_pixels(storage, path, &bmp_w, &bmp_h);
    if(!pixels) return false;

    size_t total = (size_t)bmp_w * (size_t)bmp_h;
    for(size_t i = 0; i < total; i++) {
        uint8_t r = pixels[i * 3 + 0];
        uint8_t g = pixels[i * 3 + 1];
        uint8_t b = pixels[i * 3 + 2];
        /* Check for red-ish or yellow-ish pixels */
        if((r > 128 && g < 128 && b < 128) ||
           (r > 128 && g > 128 && b < 128)) {
            free(pixels);
            return true;
        }
    }

    free(pixels);
    return false;
}
