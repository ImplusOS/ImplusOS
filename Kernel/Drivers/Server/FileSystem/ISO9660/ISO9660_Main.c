#include "Drivers/Client/FileSystem/ISO9660/ISO9660_Main.h"
#include "Debug/serial/Serial.h"
#include "kernel/config.h"
#include "Platform/io/IO_Main.h"
#include <string.h>
#include <stddef.h>

#ifdef IMPLUS_DRIVER_MODULE
#include "Drivers/Module/DriverBinary.h"

typedef struct { volatile int locked; } spinlock_t;
static inline void spinlock_init(spinlock_t *l)   { l->locked = 0; }
static inline void spinlock_lock(spinlock_t *l)   {
    while (__sync_lock_test_and_set(&l->locked, 1)) {
        while (l->locked) { __asm__ volatile("pause"); }
    }
}
static inline void spinlock_unlock(spinlock_t *l) { __sync_lock_release(&l->locked); }

static const driver_binary_t *g_driver_api = NULL;

#define disk_read           g_driver_api->disk_read
#define disk_write          g_driver_api->disk_write
#define serial_write_string g_driver_api->serial_write_string
#define serial_write_uint32 g_driver_api->serial_write_uint32

void *memcpy(void *dest, const void *src, size_t n) {
    if (!g_driver_api || !g_driver_api->memcpy) {
        char *d = dest;
        const char *s = src;
        for (size_t i = 0; i < n; i++) d[i] = s[i];
        return dest;
    }
    return g_driver_api->memcpy(dest, src, n);
}

void *memset(void *s, int c, size_t n) {
    if (!g_driver_api || !g_driver_api->memset) {
        char *p = s;
        for (size_t i = 0; i < n; i++) p[i] = (char)c;
        return s;
    }
    return g_driver_api->memset(s, c, n);
}

#else
#include "Core/sync/Spinlock.h"
#endif

#define ISO9660_SIGNATURE       "CD001"
#define ISO9660_VERSION         1
#define JOLIET_SIGNATURE        "CD001"
#define JOLIET_VERSION          1
#define ROCK_RIDGE_ID           "SP"

#define ISO9660_ATTR_DIR        0x02u

static ISO9660_CONTEXT g_iso_context;
static uint8_t g_iso_sector_buffer[ISO9660_SECTOR_BUFFER_SIZE];
static uint8_t g_iso_read_buffer[ISO9660_SECTOR_BUFFER_SIZE];
static spinlock_t g_iso_lock;

typedef struct {
    uint8_t  used;
    uint32_t current_extent;
    uint32_t remaining_size;
    uint32_t entry_offset;
} iso9660_dir_handle_t;

static iso9660_dir_handle_t g_dir_handles[ISO9660_DIR_HANDLE_MAX];

static inline uint16_t iso9660_read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t iso9660_read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint32_t iso9660_read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint32_t iso9660_read_u32_both(const uint8_t *p) {
    return iso9660_read_u32_le(p);
}

static bool iso9660_read_sector(uint32_t lba, uint8_t *buffer) {

    uint32_t base = lba * (ISO9660_SECTOR_SIZE / 512u);

    for (uint32_t i = 0; i < (ISO9660_SECTOR_SIZE / 512u); i++) {

        if (!disk_read(base + i,
                       buffer + (i * 512),
                       1)) {

            serial_write_string("ISO9660: sub-sector read failed\n");
            return false;
        }
    }

    return true;
}

static bool iso9660_validate_pvd(const uint8_t *sector) {
    if (sector[0] != 1) return false;
    if (memcmp(&sector[1], ISO9660_SIGNATURE, 5) != 0) return false;
    if (sector[6] != ISO9660_VERSION) return false;
    return true;
}

static bool iso9660_parse_pvd(const uint8_t *sector, ISO9660_CONTEXT *ctx) {
    if (!iso9660_validate_pvd(sector)) return false;
    
    ctx->root_extent = iso9660_read_u32_both(&sector[156 + 2]);
    ctx->root_size = iso9660_read_u32_both(&sector[156 + 10]);
    ctx->vol_space_size = iso9660_read_u32_both(&sector[80]);
    ctx->logical_block_size = iso9660_read_u16_le(&sector[128]);
    
    if (ctx->logical_block_size != ISO9660_SECTOR_SIZE) {
        return false;
    }
    
    return true;
}

static bool iso9660_find_pvd(ISO9660_CONTEXT *ctx) {
    for (uint32_t lba = 16; lba < 32; lba++) {
        if (!iso9660_read_sector(lba, g_iso_sector_buffer)) {
            serial_write_string("ISO9660: Failed to read sector LBA: ");
            serial_write_uint32(lba);
            serial_write_string("\n");
            return false;
        }
        
        if (iso9660_validate_pvd(g_iso_sector_buffer)) {
            serial_write_string("ISO9660: PVD found at LBA: ");
            serial_write_uint32(lba);
            serial_write_string("\n");
            ctx->pvd_extent = lba;
            if (iso9660_parse_pvd(g_iso_sector_buffer, ctx)) {
                return true;
            } else {
                serial_write_string("ISO9660: Failed to parse PVD at LBA: ");
                serial_write_uint32(lba);
                serial_write_string("\n");
            }
        }
    }
    serial_write_string("ISO9660: PVD not found in range [16, 32]\n");
    return false;
}

static bool iso9660_read_dir_record(const uint8_t *data, ISO9660_FILE *file, 
                                     uint32_t dir_extent, uint32_t dir_offset) {
    if (!data || !file) return false;
    
    uint8_t len = data[0];
    if (len == 0) return false;
    
    uint8_t name_len = data[32];
    if (name_len == 0 || name_len > ISO9660_NAME_MAX - 1) return false;
    
    file->extent = iso9660_read_u32_both(&data[2]);
    file->size = iso9660_read_u32_both(&data[10]);
    file->is_dir = (data[25] & ISO9660_ATTR_DIR) ? 1 : 0;
    file->is_symlink = 0;
    file->dir_extent = dir_extent;
    file->dir_offset = dir_offset;
    
    if (name_len == 1) {
        if (data[33] == 0x00) {
            file->name[0] = '.';
            file->name[1] = '\0';
        } else if (data[33] == 0x01) {
            file->name[0] = '.';
            file->name[1] = '.';
            file->name[2] = '\0';
        } else {
            return false;
        }
    } else {
        memcpy(file->name, &data[33], name_len);
        file->name[name_len] = '\0';
    }
    
    return true;
}

static bool iso9660_lookup_in_dir(uint32_t dir_extent, uint32_t dir_size,
                                    const char *target_name, ISO9660_FILE *out_file) {
    if (!target_name || !out_file) return false;
    
    uint32_t bytes_read = 0;
    uint32_t extent = dir_extent;
    
    while (bytes_read < dir_size) {
        if (!iso9660_read_sector(extent, g_iso_sector_buffer)) {
            return false;
        }
        
        uint32_t offset = 0;
        while (offset < ISO9660_SECTOR_SIZE && bytes_read < dir_size) {
            const uint8_t *rec = &g_iso_sector_buffer[offset];
            uint8_t rec_len = rec[0];
            
            if (rec_len == 0) {
                bytes_read += (ISO9660_SECTOR_SIZE - offset);
                offset = ISO9660_SECTOR_SIZE;
                break;
            }
            
            ISO9660_FILE temp_file;
            if (iso9660_read_dir_record(rec, &temp_file, dir_extent, bytes_read + offset)) {
                if (strcmp(temp_file.name, target_name) == 0) {
                    memcpy(out_file, &temp_file, sizeof(*out_file));
                    return true;
                }
            }
            
            offset += rec_len;
            bytes_read += rec_len;
        }
        
        extent++;
    }
    
    return false;
}

static bool iso9660_lookup_path(const char *path, ISO9660_FILE *out_entry) {
    if (!path || !out_entry || path[0] != '/') return false;
    
    uint32_t current_extent = g_iso_context.root_extent;
    uint32_t current_size = g_iso_context.root_size;
    const char *cursor = path + 1;
    
    if (*cursor == '\0') {
        out_entry->extent = current_extent;
        out_entry->size = current_size;
        out_entry->is_dir = 1;
        out_entry->name[0] = '/';
        out_entry->name[1] = '\0';
        return true;
    }
    
    while (*cursor) {
        const char *end = cursor;
        while (*end && *end != '/') end++;
        
        uint32_t name_len = end - cursor;
        if (name_len >= ISO9660_NAME_MAX) return false;
        
        char component[ISO9660_NAME_MAX];
        memcpy(component, cursor, name_len);
        component[name_len] = '\0';
        
        ISO9660_FILE found_file;
        if (!iso9660_lookup_in_dir(current_extent, current_size, component, &found_file)) {
            return false;
        }
        
        if (*end == '\0') {
            memcpy(out_entry, &found_file, sizeof(*out_entry));
            return true;
        }
        
        if (!found_file.is_dir) return false;
        
        current_extent = found_file.extent;
        current_size = found_file.size;
        cursor = end + 1;
    }
    
    return false;
}

static bool _iso9660_init(void) {
    serial_write_string("[iso9660] init enter\n");
    memset(&g_iso_context, 0, sizeof(g_iso_context));
    
    serial_write_string("[iso9660] before pvd search\n");
    if (!iso9660_find_pvd(&g_iso_context)) {
        return false;
    }
    
    serial_write_string("ISO9660 Init: PVD at sector ");
    serial_write_uint32(g_iso_context.pvd_extent);
    serial_write_string(", root extent: ");
    serial_write_uint32(g_iso_context.root_extent);
    
    return true;
}

bool iso9660_init(void) {
    spinlock_lock(&g_iso_lock);
    bool ret = _iso9660_init();
    spinlock_unlock(&g_iso_lock);
    return ret;
}

static bool _iso9660_find_file(const char *path, ISO9660_FILE *file) {
    if (!path || !file) return false;
    
    if (!iso9660_lookup_path(path, file)) {
        return false;
    }
    
    if (file->is_dir) return false;
    return true;
}

bool iso9660_find_file(const char *path, ISO9660_FILE *file) {
    spinlock_lock(&g_iso_lock);
    bool ret = _iso9660_find_file(path, file);
    spinlock_unlock(&g_iso_lock);
    return ret;
}

static bool _iso9660_read_at(ISO9660_FILE *file, uint32_t offset, uint8_t *buf, uint32_t size) {
    if (!file || !buf || offset > file->size) return false;
    if (size == 0) return true;
    
    if (offset + size > file->size) {
        size = file->size - offset;
    }
    
    uint32_t bytes_read = 0;
    uint32_t current_extent = file->extent + (offset / ISO9660_SECTOR_SIZE);
    uint32_t offset_in_sector = offset % ISO9660_SECTOR_SIZE;
    
    while (bytes_read < size) {
        if (!iso9660_read_sector(current_extent, g_iso_read_buffer)) {
            return false;
        }
        
        uint32_t can_read = ISO9660_SECTOR_SIZE - offset_in_sector;
        uint32_t to_read = (size - bytes_read) < can_read ? (size - bytes_read) : can_read;
        
        memcpy(&buf[bytes_read], &g_iso_read_buffer[offset_in_sector], to_read);
        
        bytes_read += to_read;
        offset_in_sector = 0;
        current_extent++;
    }
    
    return true;
}

bool iso9660_read_at(ISO9660_FILE *file, uint32_t offset, uint8_t *buf, uint32_t size) {
    spinlock_lock(&g_iso_lock);
    bool ret = _iso9660_read_at(file, offset, buf, size);
    spinlock_unlock(&g_iso_lock);
    return ret;
}

bool iso9660_read_file(ISO9660_FILE *file, uint8_t *buf) {
    if (!file || !buf) return false;
    return iso9660_read_at(file, 0, buf, file->size);
}

uint32_t iso9660_get_file_size(ISO9660_FILE *file) {
    return file ? file->size : 0;
}

void iso9660_list_root_files(void) {
    
}

static int32_t _iso9660_opendir(const char *path) {
    ISO9660_FILE dir_file;
    const char *dir_path = (path && *path) ? path : "/";
    
    if (!iso9660_lookup_path(dir_path, &dir_file)) {
        return -1;
    }
    
    if (!dir_file.is_dir) return -1;
    
    for (int32_t i = 0; i < ISO9660_DIR_HANDLE_MAX; i++) {
        if (!g_dir_handles[i].used) {
            g_dir_handles[i].used = 1;
            g_dir_handles[i].current_extent = dir_file.extent;
            g_dir_handles[i].remaining_size = dir_file.size;
            g_dir_handles[i].entry_offset = 0;
            return i;
        }
    }
    
    return -1;
}

int32_t iso9660_opendir(const char *path) {
    spinlock_lock(&g_iso_lock);
    int32_t ret = _iso9660_opendir(path);
    spinlock_unlock(&g_iso_lock);
    return ret;
}

static int32_t _iso9660_readdir(int32_t handle, ISO9660_DIRENT *out) {
    if (handle < 0 || handle >= ISO9660_DIR_HANDLE_MAX || !out) return -1;
    
    iso9660_dir_handle_t *h = &g_dir_handles[handle];
    if (!h->used || h->remaining_size == 0) return 0;
    
    if (!iso9660_read_sector(h->current_extent, g_iso_sector_buffer)) {
        return -1;
    }
    
    while (h->remaining_size > 0) {
        uint32_t sector_offset = h->entry_offset % ISO9660_SECTOR_SIZE;
        
        if (sector_offset == 0 && h->entry_offset > 0) {
            h->current_extent++;
            if (!iso9660_read_sector(h->current_extent, g_iso_sector_buffer)) {
                return -1;
            }
        }
        
        const uint8_t *rec = &g_iso_sector_buffer[sector_offset];
        uint8_t rec_len = rec[0];
        
        if (rec_len == 0) {
            h->remaining_size = 0;
            return 0;
        }
        
        h->entry_offset += rec_len;
        h->remaining_size -= rec_len;
        
        ISO9660_FILE temp_file;
        if (iso9660_read_dir_record(rec, &temp_file, h->current_extent, sector_offset)) {
            if ((temp_file.name[0] == '.' && temp_file.name[1] == '\0') ||
                (temp_file.name[0] == '.' && temp_file.name[1] == '.' && temp_file.name[2] == '\0')) {
                continue;
            }
            
            strcpy(out->name, temp_file.name);
            out->size = temp_file.size;
            out->extent = temp_file.extent;
            out->is_directory = temp_file.is_dir;
            return 1;
        }
    }
    
    return 0;
}

int32_t iso9660_readdir(int32_t handle, ISO9660_DIRENT *out) {
    spinlock_lock(&g_iso_lock);
    int32_t ret = _iso9660_readdir(handle, out);
    spinlock_unlock(&g_iso_lock);
    return ret;
}

static int32_t _iso9660_closedir(int32_t handle) {
    if (handle < 0 || handle >= ISO9660_DIR_HANDLE_MAX) return -1;
    memset(&g_dir_handles[handle], 0, sizeof(g_dir_handles[handle]));
    return 0;
}

int32_t iso9660_closedir(int32_t handle) {
    spinlock_lock(&g_iso_lock);
    int32_t ret = _iso9660_closedir(handle);
    spinlock_unlock(&g_iso_lock);
    return ret;
}

#ifdef IMPLUS_DRIVER_MODULE
static const iso9660_driver_t g_iso9660_driver = {
    .init                      = iso9660_init,
    .find_file                 = iso9660_find_file,
    .read_file                 = iso9660_read_file,
    .read_at                   = iso9660_read_at,
    .get_file_size             = iso9660_get_file_size,
    .list_root_files           = iso9660_list_root_files,
    .opendir                   = iso9660_opendir,
    .readdir                   = iso9660_readdir,
    .closedir                  = iso9660_closedir,
};

static void iso9660_driver_shutdown(void) {
    g_driver_api = NULL;
    memset(&g_iso_context, 0, sizeof(g_iso_context));
}

static const driver_module_descriptor_t g_iso9660_module = {
    .driver_api = &g_iso9660_driver,
    .shutdown = iso9660_driver_shutdown,
};

#undef disk_read
#undef disk_write
#undef memset
#undef memcpy
#undef serial_write_string
#undef serial_write_uint32

const driver_module_descriptor_t *driver_module_init(const driver_binary_t *api) {
    if (!api || !api->disk_read || !api->memset || !api->memcpy || !api->serial_write_string) {
        return NULL;
    }
    g_driver_api = api;
    spinlock_init(&g_iso_lock);
    return &g_iso9660_module;
}
#endif
