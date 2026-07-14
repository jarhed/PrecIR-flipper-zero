#include "precir_profiles.h"

#include "precir_protocol.h"

#include <stddef.h>
#include <string.h>

#define PRECIR_PROFILE_FILE_VERSION 1U
#define PRECIR_PROFILE_HEADER_SIZE  16U
#define PRECIR_PROFILE_STORE_SIZE   2225U

static const uint8_t precir_profile_magic[4] = {'P', 'C', 'R', 'P'};

_Static_assert(sizeof(PrecIRProfile) == PRECIR_PROFILE_RECORD_SIZE, "profile size changed");
_Static_assert(offsetof(PrecIRProfile, barcode) == 0U, "barcode offset changed");
_Static_assert(offsetof(PrecIRProfile, bmp_path) == 18U, "BMP path offset changed");
_Static_assert(offsetof(PrecIRProfile, display_size) == 274U, "size offset changed");
_Static_assert(offsetof(PrecIRProfile, color_mode) == 275U, "color offset changed");
_Static_assert(offsetof(PrecIRProfile, protocol_mode) == 276U, "protocol offset changed");
_Static_assert(offsetof(PrecIRProfile, display_page) == 277U, "page offset changed");
_Static_assert(sizeof(PrecIRProfileStore) == PRECIR_PROFILE_STORE_SIZE, "store size changed");

static size_t precir_bounded_strlen(const char* text, size_t limit) {
    if(!text) return limit;

    size_t length = 0U;
    while(length < limit && text[length] != '\0') {
        length++;
    }
    return length;
}

static uint16_t precir_read_u16_le(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t precir_read_u32_le(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void precir_write_u16_le(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void precir_write_u32_le(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t precir_profile_crc32_update(uint32_t crc, const uint8_t* data, size_t data_size) {
    for(size_t i = 0U; i < data_size; i++) {
        crc ^= data[i];
        for(uint8_t bit = 0U; bit < 8U; bit++) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? UINT32_C(0xEDB88320) : 0U);
        }
    }
    return crc;
}

static void precir_profile_encode(const PrecIRProfile* profile, uint8_t* record) {
    memset(record, 0, PRECIR_PROFILE_RECORD_SIZE);

    const size_t barcode_size = strlen(profile->barcode) + 1U;
    const size_t path_size = strlen(profile->bmp_path) + 1U;
    memcpy(record, profile->barcode, barcode_size);
    memcpy(record + PRECIR_PROFILE_BARCODE_SIZE, profile->bmp_path, path_size);

    record[274] = profile->display_size;
    record[275] = profile->color_mode;
    record[276] = profile->protocol_mode;
    record[277] = profile->display_page;
}

static bool precir_profiles_validate_store(const PrecIRProfileStore* store) {
    if(!store || store->count > PRECIR_PROFILE_MAX_COUNT) return false;

    for(uint8_t i = 0U; i < store->count; i++) {
        if(!precir_profile_is_valid(&store->profiles[i])) return false;

        for(uint8_t previous = 0U; previous < i; previous++) {
            if(strcmp(store->profiles[i].barcode, store->profiles[previous].barcode) == 0) {
                return false;
            }
        }
    }

    return true;
}

static void precir_profile_file_close(File* file) {
    if(!file) return;
    storage_file_close(file);
    storage_file_free(file);
}

void precir_profiles_init(PrecIRProfileStore* store) {
    if(store) memset(store, 0, sizeof(PrecIRProfileStore));
}

void precir_profile_init(PrecIRProfile* profile, const char* barcode) {
    if(!profile) return;

    memset(profile, 0, sizeof(PrecIRProfile));

    if(barcode &&
       precir_bounded_strlen(barcode, PRECIR_PROFILE_BARCODE_SIZE) < PRECIR_PROFILE_BARCODE_SIZE) {
        memcpy(profile->barcode, barcode, strlen(barcode) + 1U);
    }

    profile->display_size = (uint8_t)PrecIRDisplaySizeLarge;
    profile->color_mode = (uint8_t)PrecIRColorModeBWR;
    profile->protocol_mode = (uint8_t)PrecIRProtocolPP4;
    profile->display_page = 0U;
}

bool precir_profile_is_valid(const PrecIRProfile* profile) {
    if(!profile) return false;

    uint8_t plid[4];
    if(!precir_plid_from_barcode(profile->barcode, plid)) return false;

    const size_t path_length = precir_bounded_strlen(profile->bmp_path, PRECIR_PROFILE_PATH_SIZE);
    if(path_length >= PRECIR_PROFILE_PATH_SIZE) return false;

    if(path_length > 0U) {
        if(profile->bmp_path[0] != '/') return false;

        for(size_t i = 0U; i < path_length; i++) {
            const uint8_t character = (uint8_t)profile->bmp_path[i];
            if(character <= 0x1FU || character == 0x7FU) return false;
        }
    }

    if(profile->display_size > (uint8_t)PrecIRDisplaySizeLarge) return false;
    if(profile->color_mode > (uint8_t)PrecIRColorMode4C) return false;
    if(profile->protocol_mode > (uint8_t)PrecIRProtocolPP16) return false;
    if(profile->display_page > 15U) return false;

    return true;
}

int32_t precir_profiles_find(const PrecIRProfileStore* store, const char* barcode) {
    if(!store || !barcode || store->count > PRECIR_PROFILE_MAX_COUNT) return -1;

    for(uint8_t i = 0U; i < store->count; i++) {
        if(strcmp(store->profiles[i].barcode, barcode) == 0) return (int32_t)i;
    }

    return -1;
}

bool precir_profiles_add(PrecIRProfileStore* store, const char* barcode, uint8_t* out_index) {
    if(!store || !barcode) return false;

    uint8_t plid[4];
    if(!precir_plid_from_barcode(barcode, plid)) return false;
    if(store->count >= PRECIR_PROFILE_MAX_COUNT) return false;
    if(precir_profiles_find(store, barcode) >= 0) return false;

    const uint8_t index = store->count;
    precir_profile_init(&store->profiles[index], barcode);
    store->count++;

    if(out_index) *out_index = index;
    return true;
}

bool precir_profiles_delete(PrecIRProfileStore* store, uint8_t index) {
    if(!store || store->count > PRECIR_PROFILE_MAX_COUNT || index >= store->count) {
        return false;
    }

    const uint8_t remaining = (uint8_t)(store->count - index - 1U);
    if(remaining > 0U) {
        memmove(
            &store->profiles[index],
            &store->profiles[index + 1U],
            (size_t)remaining * sizeof(PrecIRProfile));
    }

    store->count--;
    memset(&store->profiles[store->count], 0, sizeof(PrecIRProfile));
    return true;
}

bool precir_profiles_load(Storage* storage, PrecIRProfileStore* store) {
    if(!storage || !store) return false;

    precir_profiles_init(store);
    if(!storage_file_exists(storage, PRECIR_PROFILE_FILE_PATH)) return true;

    File* file = storage_file_alloc(storage);
    if(!file) return false;

    bool success = false;
    if(!storage_file_open(file, PRECIR_PROFILE_FILE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        precir_profile_file_close(file);
        return false;
    }

    uint8_t header[PRECIR_PROFILE_HEADER_SIZE];
    if(storage_file_read(file, header, sizeof(header)) != sizeof(header)) goto cleanup;
    if(memcmp(header, precir_profile_magic, sizeof(precir_profile_magic)) != 0) goto cleanup;
    if(header[4] != PRECIR_PROFILE_FILE_VERSION) goto cleanup;

    const uint8_t count = header[5];
    const uint16_t record_size = precir_read_u16_le(&header[6]);
    const uint32_t payload_size = precir_read_u32_le(&header[8]);
    const uint32_t expected_crc = precir_read_u32_le(&header[12]);

    if(count > PRECIR_PROFILE_MAX_COUNT) goto cleanup;
    if(record_size != PRECIR_PROFILE_RECORD_SIZE) goto cleanup;
    if(payload_size != (uint32_t)count * PRECIR_PROFILE_RECORD_SIZE) goto cleanup;
    if(storage_file_size(file) != (uint64_t)PRECIR_PROFILE_HEADER_SIZE + payload_size) {
        goto cleanup;
    }

    store->count = count;
    uint32_t crc = UINT32_MAX;
    uint8_t record[PRECIR_PROFILE_RECORD_SIZE];

    for(uint8_t i = 0U; i < count; i++) {
        if(storage_file_read(file, record, sizeof(record)) != sizeof(record)) goto cleanup;
        crc = precir_profile_crc32_update(crc, record, sizeof(record));

        if(record[PRECIR_PROFILE_BARCODE_SIZE - 1U] != '\0') goto cleanup;
        if(!memchr(record + PRECIR_PROFILE_BARCODE_SIZE, '\0', PRECIR_PROFILE_PATH_SIZE)) {
            goto cleanup;
        }

        PrecIRProfile* profile = &store->profiles[i];
        memset(profile, 0, sizeof(PrecIRProfile));
        memcpy(profile->barcode, record, PRECIR_PROFILE_BARCODE_SIZE);
        memcpy(profile->bmp_path, record + PRECIR_PROFILE_BARCODE_SIZE, PRECIR_PROFILE_PATH_SIZE);
        profile->display_size = record[274];
        profile->color_mode = record[275];
        profile->protocol_mode = record[276];
        profile->display_page = record[277];

        if(!precir_profile_is_valid(profile)) goto cleanup;
    }

    if((~crc) != expected_crc) goto cleanup;
    if(!precir_profiles_validate_store(store)) goto cleanup;
    success = true;

cleanup:
    precir_profile_file_close(file);
    if(!success) precir_profiles_init(store);
    return success;
}

bool precir_profiles_save(Storage* storage, const PrecIRProfileStore* store) {
    if(!storage || !precir_profiles_validate_store(store)) return false;

    const FS_Error mkdir_error = storage_common_mkdir(storage, PRECIR_PROFILE_DIR_PATH);
    if(mkdir_error != FSE_OK && mkdir_error != FSE_EXIST) return false;

    uint8_t record[PRECIR_PROFILE_RECORD_SIZE];
    uint32_t crc = UINT32_MAX;
    for(uint8_t i = 0U; i < store->count; i++) {
        precir_profile_encode(&store->profiles[i], record);
        crc = precir_profile_crc32_update(crc, record, sizeof(record));
    }

    uint8_t header[PRECIR_PROFILE_HEADER_SIZE] = {0};
    memcpy(header, precir_profile_magic, sizeof(precir_profile_magic));
    header[4] = PRECIR_PROFILE_FILE_VERSION;
    header[5] = store->count;
    precir_write_u16_le(&header[6], PRECIR_PROFILE_RECORD_SIZE);
    precir_write_u32_le(&header[8], (uint32_t)store->count * PRECIR_PROFILE_RECORD_SIZE);
    precir_write_u32_le(&header[12], ~crc);

    File* file = storage_file_alloc(storage);
    if(!file) return false;

    bool success =
        storage_file_open(file, PRECIR_PROFILE_TEMP_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(success) success = storage_file_write(file, header, sizeof(header)) == sizeof(header);

    for(uint8_t i = 0U; success && i < store->count; i++) {
        precir_profile_encode(&store->profiles[i], record);
        success = storage_file_write(file, record, sizeof(record)) == sizeof(record);
    }

    if(success) success = storage_file_sync(file);
    precir_profile_file_close(file);

    if(success) {
        success = storage_common_rename(
                      storage, PRECIR_PROFILE_TEMP_PATH, PRECIR_PROFILE_FILE_PATH) == FSE_OK;
    }

    if(!success) storage_common_remove(storage, PRECIR_PROFILE_TEMP_PATH);
    return success;
}
