#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Storage Storage;
typedef struct File File;

typedef enum {
  FSAM_READ = 1,
  FSAM_WRITE = 2,
  FSAM_READ_WRITE = 3,
} FS_AccessMode;

typedef enum {
  FSOM_OPEN_EXISTING = 1,
  FSOM_OPEN_ALWAYS = 2,
  FSOM_OPEN_APPEND = 4,
  FSOM_CREATE_NEW = 8,
  FSOM_CREATE_ALWAYS = 16,
} FS_OpenMode;

typedef enum {
  FSE_OK,
  FSE_NOT_READY,
  FSE_EXIST,
  FSE_NOT_EXIST,
  FSE_INVALID_PARAMETER,
  FSE_DENIED,
  FSE_INVALID_NAME,
  FSE_INTERNAL,
  FSE_NOT_IMPLEMENTED,
  FSE_ALREADY_OPEN,
} FS_Error;

File *storage_file_alloc(Storage *storage);
void storage_file_free(File *file);
bool storage_file_open(File *file, const char *path, FS_AccessMode access_mode,
                       FS_OpenMode open_mode);
bool storage_file_close(File *file);
size_t storage_file_read(File *file, void *data, size_t size);
size_t storage_file_write(File *file, const void *data, size_t size);
uint64_t storage_file_size(File *file);
bool storage_file_sync(File *file);
bool storage_file_exists(Storage *storage, const char *path);

FS_Error storage_common_mkdir(Storage *storage, const char *path);
FS_Error storage_common_remove(Storage *storage, const char *path);
FS_Error storage_common_rename(Storage *storage, const char *old_path,
                               const char *new_path);
