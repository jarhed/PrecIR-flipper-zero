#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <storage/storage.h>

#define PRECIR_PROFILE_MAX_COUNT    8U
#define PRECIR_PROFILE_BARCODE_SIZE 18U
#define PRECIR_PROFILE_PATH_SIZE    256U
#define PRECIR_PROFILE_RECORD_SIZE  278U

#define PRECIR_PROFILE_DIR_PATH  "/data/precir"
#define PRECIR_PROFILE_FILE_PATH "/data/precir/profiles.dat"
#define PRECIR_PROFILE_TEMP_PATH "/data/precir/profiles.dat.tmp"

/** Settings and image path saved for one Pricer barcode.
 *
 * The byte-sized setting fields intentionally match the stable PCRP v1 record
 * format. Their values correspond to PrecIRDisplaySize, PrecIRColorMode, and
 * PrecIRProtocolMode respectively.
 */
typedef struct {
    char barcode[PRECIR_PROFILE_BARCODE_SIZE];
    char bmp_path[PRECIR_PROFILE_PATH_SIZE];
    uint8_t display_size;
    uint8_t color_mode;
    uint8_t protocol_mode;
    uint8_t display_page;
} PrecIRProfile;

/** Dense collection of saved profiles. Only entries below count are active. */
typedef struct {
    uint8_t count;
    PrecIRProfile profiles[PRECIR_PROFILE_MAX_COUNT];
} PrecIRProfileStore;

/** Reset a store to an empty state. */
void precir_profiles_init(PrecIRProfileStore* store);

/** Initialize one profile with the v2.1 defaults.
 *
 * Defaults are 296x128, B/W/red two-plane, PP4, page 0.
 */
void precir_profile_init(PrecIRProfile* profile, const char* barcode);

/** Validate a profile's barcode, path, and setting ranges. */
bool precir_profile_is_valid(const PrecIRProfile* profile);

/** Find a barcode and return its zero-based index, or -1 when not found. */
int32_t precir_profiles_find(const PrecIRProfileStore* store, const char* barcode);

/** Add a unique valid barcode using the default settings. */
bool precir_profiles_add(PrecIRProfileStore* store, const char* barcode, uint8_t* out_index);

/** Delete a profile and compact the remaining entries. */
bool precir_profiles_delete(PrecIRProfileStore* store, uint8_t index);

/** Load PCRP v1 data. A missing file is a valid empty store. */
bool precir_profiles_load(Storage* storage, PrecIRProfileStore* store);

/** Atomically save PCRP v1 data through profiles.dat.tmp. */
bool precir_profiles_save(Storage* storage, const PrecIRProfileStore* store);
