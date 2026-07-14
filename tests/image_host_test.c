#include "../precir/precir_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Storage {
    int unused;
};

struct File {
    FILE* stream;
};

/* Small stdio-backed implementation of the Flipper storage calls used by the
 * BMP reader. It lets the actual parser run in the host test. */
File* storage_file_alloc(Storage* storage) {
    (void)storage;
    return calloc(1, sizeof(File));
}

bool storage_file_open(File* file, const char* path, int access_mode, int open_mode) {
    (void)access_mode;
    (void)open_mode;
    file->stream = fopen(path, "rb");
    return file->stream != NULL;
}

bool storage_file_close(File* file) {
    if(!file || !file->stream) return false;
    int result = fclose(file->stream);
    file->stream = NULL;
    return result == 0;
}

void storage_file_free(File* file) {
    free(file);
}

uint64_t storage_file_size(File* file) {
    long current = ftell(file->stream);
    if(current < 0 || fseek(file->stream, 0, SEEK_END) != 0) return 0;
    long end = ftell(file->stream);
    if(end < 0 || fseek(file->stream, current, SEEK_SET) != 0) return 0;
    return (uint64_t)end;
}

size_t storage_file_read(File* file, void* data, size_t size) {
    return fread(data, 1, size, file->stream);
}

bool storage_file_seek(File* file, uint32_t offset, bool from_start) {
    return fseek(file->stream, (long)offset, from_start ? SEEK_SET : SEEK_CUR) == 0;
}

static void write_u16_le(uint8_t* data, size_t offset, uint16_t value) {
    data[offset] = (uint8_t)value;
    data[offset + 1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t* data, size_t offset, uint32_t value) {
    data[offset] = (uint8_t)value;
    data[offset + 1] = (uint8_t)(value >> 8);
    data[offset + 2] = (uint8_t)(value >> 16);
    data[offset + 3] = (uint8_t)(value >> 24);
}

static bool write_test_bmp(const char* path, uint32_t compression) {
    uint8_t header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    write_u32_le(header, 2, 78);
    write_u32_le(header, 10, 54);
    write_u32_le(header, 14, 40);
    write_u32_le(header, 18, 8);
    write_u32_le(header, 22, 1);
    write_u16_le(header, 26, 1);
    write_u16_le(header, 28, 24);
    write_u32_le(header, 30, compression);
    write_u32_le(header, 34, 24);

    /* BGR: white, black, red, yellow, then repeat. */
    const uint8_t row[24] = {
        255, 255, 255, 0, 0, 0, 0, 0, 255, 0, 255, 255,
        255, 255, 255, 0, 0, 0, 0, 0, 255, 0, 255, 255,
    };

    FILE* stream = fopen(path, "wb");
    if(!stream) return false;
    bool ok = fwrite(header, 1, sizeof(header), stream) == sizeof(header) &&
              fwrite(row, 1, sizeof(row), stream) == sizeof(row);
    if(fclose(stream) != 0) ok = false;
    return ok;
}

static bool write_test_bmp_32(const char* path) {
    uint8_t header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    write_u32_le(header, 2, 86);
    write_u32_le(header, 10, 54);
    write_u32_le(header, 14, 40);
    write_u32_le(header, 18, 8);
    write_u32_le(header, 22, 1);
    write_u16_le(header, 26, 1);
    write_u16_le(header, 28, 32);
    write_u32_le(header, 30, 0);
    write_u32_le(header, 34, 32);

    /* BGRA: white, black, red, yellow, then repeat. Alpha is ignored. */
    const uint8_t row[32] = {
        255, 255, 255, 0x11, 0, 0, 0, 0x22, 0, 0, 255, 0x33, 0, 255, 255, 0x44,
        255, 255, 255, 0x55, 0, 0, 0, 0x66, 0, 0, 255, 0x77, 0, 255, 255, 0x88,
    };

    FILE* stream = fopen(path, "wb");
    if(!stream) return false;
    bool ok = fwrite(header, 1, sizeof(header), stream) == sizeof(header) &&
              fwrite(row, 1, sizeof(row), stream) == sizeof(row);
    if(fclose(stream) != 0) ok = false;
    return ok;
}

static int check_bmp_mode(
    Storage* storage,
    const char* path,
    PrecIRColorMode mode,
    const uint8_t* expected,
    size_t expected_len) {
    uint8_t* data = NULL;
    size_t data_len = 0;
    if(!precir_image_load_bmp(storage, path, 8, 1, mode, &data, &data_len)) {
        fprintf(stderr, "BMP load failed for mode %d\n", mode);
        return 1;
    }

    int failed = data_len != expected_len || memcmp(data, expected, expected_len) != 0;
    if(failed) fprintf(stderr, "BMP mapping mismatch for mode %d\n", mode);
    free(data);
    return failed;
}

static int check_compressed(uint8_t raw, uint8_t expected_first) {
    const uint8_t input[2] = {raw, raw};
    uint8_t* encoded = NULL;
    size_t encoded_len = 0;
    if(!precir_image_rle_compress(input, sizeof(input), &encoded, &encoded_len)) {
        fprintf(stderr, "RLE rejected compressible stream %02X%02X\n", raw, raw);
        return 1;
    }

    int failed = encoded_len != 2 || encoded[0] != expected_first || encoded[1] != 0;
    if(failed) {
        fprintf(
            stderr, "RLE mismatch for %02X: length=%zu first=%02X\n", raw, encoded_len, encoded[0]);
    }
    free(encoded);
    return failed;
}

static int check_selection_one_white_payload(void) {
    static const uint8_t expected_prefix[] = {0x80, 0x00, 0x4A, 0x00};
    PrecIRImagePayload payload = {0};

    if(!precir_image_make_white_payload(296, 128, PrecIRColorModeBWR, &payload)) {
        fputs("selection 1 white payload generation failed\n", stderr);
        return 1;
    }

    int failed = payload.compression != 2 || payload.data_len != 20 ||
                 memcmp(payload.data, expected_prefix, sizeof(expected_prefix)) != 0;
    for(size_t i = sizeof(expected_prefix); !failed && i < payload.data_len; i++) {
        failed = payload.data[i] != 0;
    }
    if(failed) {
        fputs("selection 1 white payload did not match the recovered vector\n", stderr);
    }
    precir_image_payload_free(&payload);

    const uint16_t invalid_dimensions[][2] = {{0, 128}, {296, 0}};
    for(size_t i = 0; i < sizeof(invalid_dimensions) / sizeof(invalid_dimensions[0]); i++) {
        payload.data = (uint8_t*)0x1;
        payload.data_len = 123;
        payload.compression = 7;
        if(precir_image_make_white_payload(
               invalid_dimensions[i][0], invalid_dimensions[i][1], PrecIRColorModeBWR, &payload) ||
           payload.data != NULL || payload.data_len != 0 || payload.compression != 0) {
            fputs("invalid white-payload dimensions were not rejected cleanly\n", stderr);
            failed = 1;
        }
    }

    return failed;
}

int main(void) {
    int failures = 0;
    if(!precir_image_self_test()) {
        fputs("image self-test failed\n", stderr);
        failures++;
    }

    failures += check_compressed(0xFF, 0x84);
    failures += check_compressed(0x00, 0x04);
    failures += check_selection_one_white_payload();

    const uint8_t non_smaller[] = {0xFF, 0x00, 0xAA};
    for(size_t i = 0; i < sizeof(non_smaller); i++) {
        uint8_t* encoded = (uint8_t*)0x1;
        size_t encoded_len = 123;
        if(precir_image_rle_compress(&non_smaller[i], 1, &encoded, &encoded_len) ||
           encoded != NULL || encoded_len != 0) {
            fputs("RLE accepted a non-smaller one-byte stream\n", stderr);
            failures++;
        }
    }

    size_t padded_len = 3;
    uint8_t* padded = malloc(padded_len);
    if(!padded) return 2;
    padded[0] = 0x11;
    padded[1] = 0x22;
    padded[2] = 0x33;
    if(!precir_image_pad(&padded, &padded_len) || padded_len != 20 || padded[0] != 0x11 ||
       padded[1] != 0x22 || padded[2] != 0x33) {
        fputs("20-byte padding failed\n", stderr);
        failures++;
    } else {
        for(size_t i = 3; i < padded_len; i++) {
            if(padded[i] != 0) {
                fputs("padding was not zero-filled\n", stderr);
                failures++;
                break;
            }
        }
    }
    free(padded);

    const char* bmp_path = "precir_image_host_test.bmp";
    struct Storage storage = {0};
    if(!write_test_bmp(bmp_path, 0)) {
        fputs("could not create BMP fixture\n", stderr);
        return 2;
    }

    const uint8_t expected_bw[] = {0x99};
    const uint8_t expected_bwr[] = {0xAA, 0xCC};
    const uint8_t expected_4c[] = {0xAA, 0xCC};
    failures +=
        check_bmp_mode(&storage, bmp_path, PrecIRColorModeBW, expected_bw, sizeof(expected_bw));
    failures +=
        check_bmp_mode(&storage, bmp_path, PrecIRColorModeBWR, expected_bwr, sizeof(expected_bwr));
    failures +=
        check_bmp_mode(&storage, bmp_path, PrecIRColorMode4C, expected_4c, sizeof(expected_4c));

    if(!write_test_bmp_32(bmp_path)) {
        fputs("could not create 32-bit BMP fixture\n", stderr);
        remove(bmp_path);
        return 2;
    }
    failures +=
        check_bmp_mode(&storage, bmp_path, PrecIRColorModeBW, expected_bw, sizeof(expected_bw));
    failures +=
        check_bmp_mode(&storage, bmp_path, PrecIRColorModeBWR, expected_bwr, sizeof(expected_bwr));
    failures +=
        check_bmp_mode(&storage, bmp_path, PrecIRColorMode4C, expected_4c, sizeof(expected_4c));

    if(!write_test_bmp(bmp_path, 1)) {
        fputs("could not create invalid BMP fixture\n", stderr);
        remove(bmp_path);
        return 2;
    }
    uint8_t* rejected_data = (uint8_t*)0x1;
    size_t rejected_len = 123;
    if(precir_image_load_bmp(
           &storage, bmp_path, 8, 1, PrecIRColorModeBW, &rejected_data, &rejected_len) ||
       rejected_data != NULL || rejected_len != 0) {
        fputs("compressed BMP was not rejected cleanly\n", stderr);
        failures++;
    }
    remove(bmp_path);

    if(failures == 0) puts("image host tests passed");
    return failures == 0 ? 0 : 1;
}
