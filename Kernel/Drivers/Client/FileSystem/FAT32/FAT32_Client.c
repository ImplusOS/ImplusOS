#include "FAT32_Main.h"

#include "Drivers/Module/DriverBinary.h"
#include "Drivers/Module/DriverManager.h"
#include "Debug/serial/Serial.h"

#include <stdbool.h>
#include <stdint.h>

static const fat32_driver_t *g_fat32_driver = NULL;
static uint8_t g_fat32_initialized = 0;
static uint8_t g_fat32_init_failed = 0;

static bool ensure_fat32_initialized() {
    const device_t *device = driver_manager_find(DEVICE_TYPE_FILESYSTEM, "FAT32_Driver.ELF");
    const fat32_driver_t *driver = device ? (const fat32_driver_t *)device->ops : NULL;

    if (driver == NULL) {
        g_fat32_driver = NULL;
        g_fat32_initialized = 0;
        return false;
    }

    if (g_fat32_driver != driver) {
        g_fat32_driver = driver;
        g_fat32_initialized = 0;
        g_fat32_init_failed = 0;
    }

    if (g_fat32_init_failed) {
        return false;
    }
    if (g_fat32_initialized) {
        return true;
    }

    if (g_fat32_driver->init == NULL) {
        return false;
    }
    if (!g_fat32_driver->init()) {
        g_fat32_init_failed = 1;
        return false;
    }
    g_fat32_initialized = 1;
    return true;
}

bool fat32_init()
{
    return ensure_fat32_initialized();
}

bool fat32_find_file(const char *filename, FAT32_FILE *file)
{
    if (!ensure_fat32_initialized()) {
        return false;
    }
    return g_fat32_driver->find_file(filename, file);
}

bool fat32_read_file(FAT32_FILE *file, uint8_t *buffer)
{
    if (!ensure_fat32_initialized()) {
        return false;
    }
    return g_fat32_driver->read_file(file, buffer);
}

bool fat32_write_file(FAT32_FILE *file, const uint8_t *buffer)
{
    if (!ensure_fat32_initialized()) {
        return false;
    }
    return g_fat32_driver->write_file(file, buffer);
}

bool fat32_read_at(FAT32_FILE *file, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    if (!ensure_fat32_initialized()) {
        return false;
    }
    return g_fat32_driver->read_at(file, offset, buffer, size);
}

bool fat32_write_at(FAT32_FILE *file, uint32_t offset, const uint8_t *buffer, uint32_t size)
{
    if (!ensure_fat32_initialized()) {
        return false;
    }
    return g_fat32_driver->write_at(file, offset, buffer, size);
}

bool fat32_truncate(FAT32_FILE *file, uint32_t new_size)
{
    if (!ensure_fat32_initialized()) {
        return false;
    }
    return g_fat32_driver->truncate(file, new_size);
}

uint32_t fat32_get_file_size(FAT32_FILE *file)
{
    if (!ensure_fat32_initialized()) {
        return 0;
    }
    return g_fat32_driver->get_file_size(file);
}

void fat32_list_root_files(void)
{
    if (!ensure_fat32_initialized()) {
        return;
    }
    g_fat32_driver->list_root_files();
}

bool fat32_creat(const char *path)
{
    if (!ensure_fat32_initialized()) {
        return false;
    }
    return g_fat32_driver->creat(path);
}

bool fat32_mkdir(const char *path)
{
    if (!ensure_fat32_initialized()) {
        return false;
    }
    return g_fat32_driver->mkdir(path);
}

int32_t fat32_opendir(const char *path)
{
    if (!ensure_fat32_initialized()) {
        return -1;
    }
    return g_fat32_driver->opendir(path);
}

int32_t fat32_readdir(int32_t dir_handle, FAT32_DIRENT *out_entry)
{
    if (!ensure_fat32_initialized()) {
        return -1;
    }
    return g_fat32_driver->readdir(dir_handle, out_entry);
}

int32_t fat32_closedir(int32_t dir_handle)
{
    if (!ensure_fat32_initialized()) {
        return -1;
    }
    return g_fat32_driver->closedir(dir_handle);
}

bool fat32_unlink(const char *path)
{
    if (!ensure_fat32_initialized()) {
        return false;
    }
    return g_fat32_driver->unlink(path);
}

void fat32_set_case_sensitive_lookup(bool enabled)
{
    if (!ensure_fat32_initialized()) {
        return;
    }
    g_fat32_driver->set_case_sensitive_lookup(enabled);
}

bool fat32_get_case_sensitive_lookup(void)
{
    if (!ensure_fat32_initialized()) {
        return false;
    }
    return g_fat32_driver->get_case_sensitive_lookup();
}
