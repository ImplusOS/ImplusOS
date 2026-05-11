#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "FileSystem/FAT32_BPB.h"

typedef struct FAT32_FILE FAT32_FILE;
typedef struct FAT32_DIRENT FAT32_DIRENT;

typedef struct {
    bool (*init)(const FAT32_BPB *initial_bpb);
    bool (*find_file)(const char *path, FAT32_FILE *file);
    bool (*read_file)(FAT32_FILE *file, uint8_t *buffer);
    bool (*write_file)(FAT32_FILE *file, const uint8_t *buffer);
} filesystem_ops_t;
