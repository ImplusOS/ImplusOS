#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ISO9660_SECTOR_SIZE                 2048u
#define ISO9660_PATH_MAX                    512u
#define ISO9660_NAME_MAX                    260u
#define ISO9660_DIR_HANDLE_MAX              256u
#define ISO9660_SECTOR_BUFFER_SIZE          8192u

typedef struct {
    uint32_t extent;
    uint32_t size;
    uint8_t  is_dir;
    uint8_t  is_symlink;
    uint32_t dir_extent;
    uint32_t dir_offset;
    char     name[ISO9660_NAME_MAX];
} ISO9660_FILE;

typedef struct {
    char     name[ISO9660_NAME_MAX];
    uint32_t size;
    uint32_t extent;
    uint8_t  is_directory;
} ISO9660_DIRENT;

typedef struct {
    uint32_t pvd_extent;
    uint32_t root_extent;
    uint32_t root_size;
    uint32_t vol_space_size;
    uint16_t logical_block_size;
} ISO9660_CONTEXT;

bool     iso9660_init(void);
bool     iso9660_find_file(const char *path, ISO9660_FILE *out);
uint32_t iso9660_get_file_size(ISO9660_FILE *file);
bool     iso9660_read_file(ISO9660_FILE *file, uint8_t *buf);
bool     iso9660_read_at(ISO9660_FILE *file, uint32_t offset, uint8_t *buf, uint32_t size);
int32_t  iso9660_opendir(const char *path);
int32_t  iso9660_readdir(int32_t handle, ISO9660_DIRENT *out);
int32_t  iso9660_closedir(int32_t handle);
void     iso9660_list_root_files(void);

typedef struct {
    bool     (*init)(void);
    bool     (*find_file)(const char *, ISO9660_FILE *);
    bool     (*read_file)(ISO9660_FILE *, uint8_t *);
    bool     (*read_at)(ISO9660_FILE *, uint32_t, uint8_t *, uint32_t);
    uint32_t (*get_file_size)(ISO9660_FILE *);
    void     (*list_root_files)(void);
    int32_t  (*opendir)(const char *);
    int32_t  (*readdir)(int32_t, ISO9660_DIRENT *);
    int32_t  (*closedir)(int32_t);
} iso9660_driver_t;
