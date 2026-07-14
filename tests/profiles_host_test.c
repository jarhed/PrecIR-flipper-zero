#include "../precir/precir_profiles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_FILE_CAPACITY 4096U

typedef struct {
  bool exists;
  size_t size;
  uint8_t data[FAKE_FILE_CAPACITY];
} FakeFileData;

struct Storage {
  FakeFileData primary;
  FakeFileData temporary;
};

typedef enum {
  FakeFileNone,
  FakeFilePrimary,
  FakeFileTemporary,
} FakeFileKind;

struct File {
  Storage *storage;
  FakeFileKind kind;
  size_t position;
  bool open;
  FS_AccessMode access;
};

static FakeFileData *fake_file_data(File *file) {
  if (!file || !file->storage)
    return NULL;
  if (file->kind == FakeFilePrimary)
    return &file->storage->primary;
  if (file->kind == FakeFileTemporary)
    return &file->storage->temporary;
  return NULL;
}

static FakeFileKind fake_file_kind(const char *path) {
  if (strcmp(path, PRECIR_PROFILE_FILE_PATH) == 0)
    return FakeFilePrimary;
  if (strcmp(path, PRECIR_PROFILE_TEMP_PATH) == 0)
    return FakeFileTemporary;
  return FakeFileNone;
}

File *storage_file_alloc(Storage *storage) {
  File *file = calloc(1U, sizeof(File));
  if (file)
    file->storage = storage;
  return file;
}

void storage_file_free(File *file) { free(file); }

bool storage_file_open(File *file, const char *path, FS_AccessMode access_mode,
                       FS_OpenMode open_mode) {
  if (!file || !path)
    return false;

  file->kind = fake_file_kind(path);
  file->position = 0U;
  file->access = access_mode;
  FakeFileData *data = fake_file_data(file);
  if (!data)
    return false;

  if (open_mode == FSOM_OPEN_EXISTING) {
    if (!data->exists)
      return false;
  } else if (open_mode == FSOM_CREATE_ALWAYS) {
    data->exists = true;
    data->size = 0U;
    memset(data->data, 0, sizeof(data->data));
  } else {
    return false;
  }

  file->open = true;
  return true;
}

bool storage_file_close(File *file) {
  if (!file)
    return false;
  file->open = false;
  return true;
}

size_t storage_file_read(File *file, void *output, size_t size) {
  FakeFileData *data = fake_file_data(file);
  if (!file || !file->open || !(file->access & FSAM_READ) || !data ||
      file->position > data->size) {
    return 0U;
  }

  size_t available = data->size - file->position;
  if (size > available)
    size = available;
  memcpy(output, data->data + file->position, size);
  file->position += size;
  return size;
}

size_t storage_file_write(File *file, const void *input, size_t size) {
  FakeFileData *data = fake_file_data(file);
  if (!file || !file->open || !(file->access & FSAM_WRITE) || !data ||
      size > FAKE_FILE_CAPACITY - file->position) {
    return 0U;
  }

  memcpy(data->data + file->position, input, size);
  file->position += size;
  if (file->position > data->size)
    data->size = file->position;
  return size;
}

uint64_t storage_file_size(File *file) {
  FakeFileData *data = fake_file_data(file);
  return data ? data->size : 0U;
}

bool storage_file_sync(File *file) { return file && file->open; }

bool storage_file_exists(Storage *storage, const char *path) {
  if (!storage || !path)
    return false;
  const FakeFileKind kind = fake_file_kind(path);
  if (kind == FakeFilePrimary)
    return storage->primary.exists;
  if (kind == FakeFileTemporary)
    return storage->temporary.exists;
  return false;
}

FS_Error storage_common_mkdir(Storage *storage, const char *path) {
  if (!storage || !path || strcmp(path, PRECIR_PROFILE_DIR_PATH) != 0) {
    return FSE_INVALID_PARAMETER;
  }
  return FSE_EXIST;
}

FS_Error storage_common_remove(Storage *storage, const char *path) {
  if (!storage || !path)
    return FSE_INVALID_PARAMETER;

  const FakeFileKind kind = fake_file_kind(path);
  FakeFileData *data = NULL;
  if (kind == FakeFilePrimary)
    data = &storage->primary;
  if (kind == FakeFileTemporary)
    data = &storage->temporary;
  if (!data)
    return FSE_NOT_EXIST;

  memset(data, 0, sizeof(*data));
  return FSE_OK;
}

FS_Error storage_common_rename(Storage *storage, const char *old_path,
                               const char *new_path) {
  if (!storage || !old_path || !new_path)
    return FSE_INVALID_PARAMETER;
  if (fake_file_kind(old_path) != FakeFileTemporary ||
      fake_file_kind(new_path) != FakeFilePrimary ||
      !storage->temporary.exists) {
    return FSE_NOT_EXIST;
  }

  storage->primary = storage->temporary;
  memset(&storage->temporary, 0, sizeof(storage->temporary));
  return FSE_OK;
}

static int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition);  \
      failures++;                                                              \
    }                                                                          \
  } while (false)

static void test_collection_operations(void) {
  PrecIRProfileStore store;
  memset(&store, 0xA5, sizeof(store));
  precir_profiles_init(&store);
  CHECK(store.count == 0U);

  uint8_t index = UINT8_MAX;
  CHECK(precir_profiles_add(&store, "G4591371776312423", &index));
  CHECK(index == 0U);
  CHECK(store.count == 1U);
  CHECK(store.profiles[0].display_size == 1U);
  CHECK(store.profiles[0].color_mode == 1U);
  CHECK(store.profiles[0].protocol_mode == 0U);
  CHECK(store.profiles[0].display_page == 0U);
  CHECK(precir_profiles_find(&store, "G4591371776312423") == 0);
  CHECK(!precir_profiles_add(&store, "G4591371776312423", NULL));
  CHECK(!precir_profiles_add(&store, "invalid", NULL));

  CHECK(precir_profiles_add(&store, "N4088313815413287", &index));
  CHECK(index == 1U);
  CHECK(precir_profiles_delete(&store, 0U));
  CHECK(store.count == 1U);
  CHECK(strcmp(store.profiles[0].barcode, "N4088313815413287") == 0);
  CHECK(store.profiles[1].barcode[0] == '\0');
  CHECK(!precir_profiles_delete(&store, 1U));
}

static void test_profile_validation(void) {
  PrecIRProfile profile;
  precir_profile_init(&profile, "G4591371776312423");
  CHECK(precir_profile_is_valid(&profile));

  strcpy(profile.bmp_path, "relative.bmp");
  CHECK(!precir_profile_is_valid(&profile));

  memset(profile.bmp_path, 'A', sizeof(profile.bmp_path));
  profile.bmp_path[0] = '/';
  CHECK(!precir_profile_is_valid(&profile));

  memset(profile.bmp_path, 0, sizeof(profile.bmp_path));
  profile.display_page = 16U;
  CHECK(!precir_profile_is_valid(&profile));
}

static void test_file_round_trip(void) {
  static const uint8_t expected_empty_header[16] = {
      0x50, 0x43, 0x52, 0x50, 0x01, 0x00, 0x16, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  static const uint8_t expected_header[16] = {
      0x50, 0x43, 0x52, 0x50, 0x01, 0x01, 0x16, 0x01,
      0x16, 0x01, 0x00, 0x00, 0x89, 0x7E, 0x3C, 0x14,
  };
  static const char barcode[] = "G4591371776312423";
  static const char path[] = "/ext/PrecIR/test.bmp";

  Storage storage = {0};
  PrecIRProfileStore store;
  precir_profiles_init(&store);
  CHECK(precir_profiles_save(&storage, &store));
  CHECK(storage.primary.size == sizeof(expected_empty_header));
  CHECK(memcmp(storage.primary.data, expected_empty_header,
               sizeof(expected_empty_header)) == 0);

  CHECK(precir_profiles_add(&store, barcode, NULL));
  strcpy(store.profiles[0].bmp_path, path);

  /* Bytes after the terminator are not semantically part of the path. Saving
     must canonicalize them back to zero exactly as the v2.1 FAP does. */
  store.profiles[0].bmp_path[100] = (char)0xA5;

  CHECK(precir_profiles_save(&storage, &store));
  CHECK(storage.primary.exists);
  CHECK(!storage.temporary.exists);
  CHECK(storage.primary.size == 16U + PRECIR_PROFILE_RECORD_SIZE);
  CHECK(memcmp(storage.primary.data, expected_header,
               sizeof(expected_header)) == 0);

  const uint8_t *record = storage.primary.data + 16U;
  CHECK(memcmp(record, barcode, sizeof(barcode)) == 0);
  CHECK(memcmp(record + PRECIR_PROFILE_BARCODE_SIZE, path, sizeof(path)) == 0);
  CHECK(record[PRECIR_PROFILE_BARCODE_SIZE + 100U] == 0U);
  CHECK(record[274] == 1U && record[275] == 1U && record[276] == 0U &&
        record[277] == 0U);

  PrecIRProfileStore loaded;
  memset(&loaded, 0x5A, sizeof(loaded));
  CHECK(precir_profiles_load(&storage, &loaded));
  CHECK(loaded.count == 1U);
  CHECK(strcmp(loaded.profiles[0].barcode, barcode) == 0);
  CHECK(strcmp(loaded.profiles[0].bmp_path, path) == 0);

  storage.primary.data[12] ^= 1U;
  CHECK(!precir_profiles_load(&storage, &loaded));
  CHECK(loaded.count == 0U);

  Storage missing = {0};
  memset(&loaded, 0x5A, sizeof(loaded));
  CHECK(precir_profiles_load(&missing, &loaded));
  CHECK(loaded.count == 0U);
}

int main(void) {
  CHECK(sizeof(PrecIRProfile) == PRECIR_PROFILE_RECORD_SIZE);
  CHECK(sizeof(PrecIRProfileStore) == 2225U);

  test_collection_operations();
  test_profile_validation();
  test_file_round_trip();

  if (failures == 0)
    puts("profile host tests passed");
  return failures == 0 ? 0 : 1;
}
