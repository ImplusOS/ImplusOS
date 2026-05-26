#include "../../../BootManager/BIOS/BIOS_Handoff.h"
#include "../../../BootManager/ISO9660.h"
#include <stdint.h>
#include <stddef.h>

#define SECTOR_SIZE 512u
#define ISO_SECTOR_SIZE 2048u

extern int bios_read_sector32(uint32_t drive, uint32_t lba_low, uint32_t lba_high, void *buffer);

static uint8_t g_sector[ISO_SECTOR_SIZE] __attribute__((aligned(16)));

static int read_absolute(uint8_t drive, uint64_t lba, void *buffer) {
    return bios_read_sector32(drive, (uint32_t)lba, (uint32_t)(lba >> 32), buffer);
}

static int bios_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) break;
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static void iso_normalize_name(char *name) {
    for (int i = 0; name[i]; i++) {
        if (name[i] == ';') {
            name[i] = 0;
            break;
        }
    }
    int len = 0;
    while (name[len]) len++;
    if (len > 0 && name[len - 1] == '.') name[len - 1] = 0;
}

static int iso_find_in_dir(uint8_t drive, uint32_t dir_lba, uint32_t dir_size, const char *name, uint32_t *lba_out, uint32_t *size_out, int *is_dir) {
    uint32_t sectors = (dir_size + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE;
    for (uint32_t s = 0; s < sectors; ++s) {
        for (int b = 0; b < 4; ++b) {
            if (read_absolute(drive, (uint64_t)dir_lba * 4 + s * 4 + b, g_sector + b * 512) != 0) return -1;
        }
        
        uint8_t *ptr = g_sector;
        while (ptr < g_sector + ISO_SECTOR_SIZE && *ptr != 0) {
            ISO9660_DIR_RECORD *rec = (ISO9660_DIR_RECORD *)ptr;
            if (rec->length == 0 || ptr + rec->length > g_sector + ISO_SECTOR_SIZE) break;
            if (rec->name_length > 0) {
                char entry_name[256];
                int len = rec->name_length;
                if (len > 255) len = 255;
                for (int i = 0; i < len; i++) entry_name[i] = rec->name[i];
                entry_name[len] = 0;
                iso_normalize_name(entry_name);

                if (bios_strcasecmp(entry_name, name) == 0) {
                    *lba_out = rec->extent_lba_le;
                    *size_out = rec->data_length_le;
                    *is_dir = (rec->flags & 2) != 0;
                    return 0;
                }
            }
            ptr += rec->length;
        }
    }
    return -1;
}

static int iso_find_path(uint8_t drive, uint32_t root_lba, uint32_t root_size, const char *path, uint32_t *lba_out, uint32_t *size_out) {
    uint32_t cur_lba = root_lba;
    uint32_t cur_size = root_size;
    const char *p = path;
    while (*p == '/') ++p;
    
    while (*p) {
        char component[128];
        int n = 0;
        while (p[n] && p[n] != '/' && n < 127) {
            component[n] = p[n];
            ++n;
        }
        component[n] = 0;
        
        int is_dir = 0;
        if (iso_find_in_dir(drive, cur_lba, cur_size, component, &cur_lba, &cur_size, &is_dir) != 0) return -1;
        
        p += n;
        while (*p == '/') ++p;
        if (*p && !is_dir) return -1;
    }
    
    *lba_out = cur_lba;
    *size_out = cur_size;
    return 0;
}

void bootmanager_bios_main(BIOS_BOOT_PARAMS *params) {
    if (!params || params->signature != BIOS_BOOT_PARAMS_SIGNATURE) {
        for (;;) __asm__ volatile("hlt");
    }

    uint8_t drive = params->boot_drive;
    if (read_absolute(drive, 64, g_sector) != 0) {
        for (;;) __asm__ volatile("hlt");
    }

    ISO9660_PVD *pvd = (ISO9660_PVD *)g_sector;
    if (pvd->type != 1 || pvd->identifier[0] != 'C' || pvd->identifier[1] != 'D' ||
        pvd->identifier[2] != '0' || pvd->identifier[3] != '0' || pvd->identifier[4] != '1') {
        for (;;) __asm__ volatile("hlt");
    }

    uint32_t root_lba = pvd->root_dir_record.extent_lba_le;
    uint32_t root_size = pvd->root_dir_record.data_length_le;

    uint32_t bm_lba, bm_size;
    if (iso_find_path(drive, root_lba, root_size, "/BootManager/BootManager_BIOS.BIN", &bm_lba, &bm_size) != 0) {
        for (;;) __asm__ volatile("hlt");
    }

    uint8_t *dst = (uint8_t *)0x10000;
    uint32_t remaining = bm_size;
    uint32_t cur_lba = bm_lba;
    
    while (remaining > 0) {
        for (int b = 0; b < 4; ++b) {
            if (read_absolute(drive, (uint64_t)cur_lba * 4 + b, g_sector + b * 512) != 0) {
                for (;;) __asm__ volatile("hlt");
            }
        }
        uint32_t copy = (remaining < ISO_SECTOR_SIZE) ? remaining : ISO_SECTOR_SIZE;
        for (uint32_t i = 0; i < copy; i++) dst[i] = g_sector[i];
        dst += copy;
        remaining -= copy;
        cur_lba++;
    }
    
    void (*entry)(BIOS_BOOT_PARAMS *) = (void (*)(BIOS_BOOT_PARAMS *))0x10000;
    entry(params);

    for (;;) __asm__ volatile("hlt");
}
