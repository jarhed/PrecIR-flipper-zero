#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Storage Storage;
typedef struct File File;

enum {
    FSAM_READ = 0,
    FSOM_OPEN_EXISTING = 0,
};

File* storage_file_alloc(Storage* storage);
bool storage_file_open(File* file, const char* path, int access_mode, int open_mode);
bool storage_file_close(File* file);
void storage_file_free(File* file);
uint64_t storage_file_size(File* file);
size_t storage_file_read(File* file, void* data, size_t size);
bool storage_file_seek(File* file, uint32_t offset, bool from_start);
