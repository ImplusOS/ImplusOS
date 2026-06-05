#include "ISO9660_Main.h"
#include "Drivers/Module/DriverManager.h"

static const iso9660_driver_t *get_driver(void) {
    const device_t *device = driver_manager_find(DEVICE_TYPE_FILESYSTEM, "ISO9660_Driver.ELF");
    return device ? (const iso9660_driver_t *)device->ops : NULL;
}

bool iso9660_init(void) {
    const iso9660_driver_t *drv = get_driver();
    if (!drv || !drv->init) return false;
    return drv->init();
}

bool iso9660_find_file(const char *path, ISO9660_FILE *file) {
    const iso9660_driver_t *drv = get_driver();
    if (!drv || !drv->find_file) return false;
    return drv->find_file(path, file);
}

bool iso9660_read_file(ISO9660_FILE *file, uint8_t *buf) {
    const iso9660_driver_t *drv = get_driver();
    if (!drv || !drv->read_file) return false;
    return drv->read_file(file, buf);
}

bool iso9660_read_at(ISO9660_FILE *file, uint32_t offset, uint8_t *buf, uint32_t size) {
    const iso9660_driver_t *drv = get_driver();
    if (!drv || !drv->read_at) return false;
    return drv->read_at(file, offset, buf, size);
}

uint32_t iso9660_get_file_size(ISO9660_FILE *file) {
    const iso9660_driver_t *drv = get_driver();
    if (!drv || !drv->get_file_size) return 0;
    return drv->get_file_size(file);
}

void iso9660_list_root_files(void) {
    const iso9660_driver_t *drv = get_driver();
    if (!drv || !drv->list_root_files) return;
    drv->list_root_files();
}

int32_t iso9660_opendir(const char *path) {
    const iso9660_driver_t *drv = get_driver();
    if (!drv || !drv->opendir) return -1;
    return drv->opendir(path);
}

int32_t iso9660_readdir(int32_t handle, ISO9660_DIRENT *out) {
    const iso9660_driver_t *drv = get_driver();
    if (!drv || !drv->readdir) return -1;
    return drv->readdir(handle, out);
}

int32_t iso9660_closedir(int32_t handle) {
    const iso9660_driver_t *drv = get_driver();
    if (!drv || !drv->closedir) return -1;
    return drv->closedir(handle);
}
