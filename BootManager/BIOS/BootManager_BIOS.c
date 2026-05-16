#include "BIOS_Handoff.h"
#include "../BootManager_libc/include/string.h"
#include "../../Kernel/FileSystem/FAT32_BPB.h"
#include <stdint.h>
#include <stddef.h>

#define SECTOR_SIZE 512u
#include "../BootInfo.h"
#include "../ElfDefs.h"

// Math functions for stb_truetype
static double sqrt(double x) {
    if (x <= 0) return 0;
    double r = x;
    for (int i = 0; i < 10; i++) r = 0.5 * (r + x / r);
    return r;
}
static double pow(double x, double y) { (void)y; return x; }
static double fmod(double x, double y) { int q = (int)(x / y); return x - q * y; }
static double cos(double x) { (void)x; return 1.0; }
static double acos(double x) { (void)x; return 0.0; }
static double fabs(double x) { return (x < 0) ? -x : x; }
static double floor(double x) { return (double)(int)x; }
static double ceil(double x) { return (double)(int)(x + 0.999999); }

static uint32_t g_alloc_ptr = 0x04000000;
static void *bios_malloc(size_t size) {
    if (size == 0) return NULL;
    void *ptr = (void *)(uintptr_t)g_alloc_ptr;
    g_alloc_ptr = (g_alloc_ptr + size + 4095) & ~4095u;
    return ptr;
}
static void bios_free(void *ptr) { (void)ptr; }

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STB_TRUETYPE_NO_STDIO
#define STBTT_assert(x)
#define STBTT_memcpy  memcpy
#define STBTT_memset  memset
#define STBTT_memmove memmove
#define STBTT_strlen  strlen

#define STBTT_sqrt(x)   sqrt(x)
#define STBTT_pow(x,y)  pow(x,y)
#define STBTT_fmod(x,y) fmod(x,y)
#define STBTT_cos(x)    cos(x)
#define STBTT_acos(x)   acos(x)
#define STBTT_fabs(x)   fabs(x)
#define STBTT_ifloor(x) ((int)floor(x))
#define STBTT_iceil(x)  ((int)ceil(x))

#define STBTT_malloc(x,u) bios_malloc(x)
#define STBTT_free(x,u)   bios_free(x)
#define STBTT__NOTUSED(v) (void)sizeof(v)
#define STB_TRUETYPE_NO_MATH

#include "../../Thirdparty/stb_truetype.h"

typedef struct {
    uint8_t boot_ind;
    uint8_t start_chs[3];
    uint8_t sys_id;
    uint8_t end_chs[3];
    uint32_t start_lba;
    uint32_t sectors;
} __attribute__((packed)) MBR_PARTITION;

typedef struct {
    uint8_t jump[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors16;
    uint8_t media;
    uint16_t fat_size16;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
    uint32_t fat_size32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
} __attribute__((packed)) FAT32_BOOT_SECTOR;

typedef struct {
    char Name[11];
    uint8_t Attr;
    uint8_t Reserved;
    uint8_t CreateTimeTenths;
    uint16_t CreateTime;
    uint16_t CreateDate;
    uint16_t LastAccessDate;
    uint16_t FirstClusterHigh;
    uint16_t WriteTime;
    uint16_t WriteDate;
    uint16_t FirstClusterLow;
    uint32_t FileSize;
} __attribute__((packed)) FAT32_DIR_ENTRY;

typedef struct {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t zero;
    uint16_t name3[2];
} __attribute__((packed)) FAT32_LFN_ENTRY;

typedef struct {
    FAT32_BPB bpb;
    uint8_t boot_drive;
    uint64_t partition_lba;
    uint32_t first_data_sector;
    uint32_t bytes_per_cluster;
} BIOS_FAT32;



typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attr;
} __attribute__((packed)) E820_ENTRY;

extern int bios_read_sector32(uint32_t drive, uint32_t lba_low, uint32_t lba_high, void *buffer);
extern void bios_enter_kernel64(uint32_t entry_low, uint32_t boot_info_low) __attribute__((noreturn));

static uint8_t g_sector[SECTOR_SIZE] __attribute__((aligned(16)));
static EFI_MEMORY_DESCRIPTOR g_memory_map[128] __attribute__((aligned(16)));

static BOOT_INFO g_boot_info __attribute__((aligned(16)));

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void bios_serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static void bios_putc(char c) {
    if (c == '\n') bios_putc('\r');
    while ((inb(0x3F8 + 5) & 0x20) == 0) {}
    outb(0x3F8, (uint8_t)c);
}

static void bios_puts(const char *s) {
    while (*s) bios_putc(*s++);
}

static int read_sector(BIOS_FAT32 *fs, uint64_t sector, void *buffer) {
    uint64_t lba = fs->partition_lba + sector;
    return bios_read_sector32(fs->boot_drive, (uint32_t)lba, (uint32_t)(lba >> 32), buffer);
}

static int read_absolute(uint8_t drive, uint64_t lba, void *buffer) {
    return bios_read_sector32(drive, (uint32_t)lba, (uint32_t)(lba >> 32), buffer);
}

static void bpb_from_sector(const FAT32_BOOT_SECTOR *bs, FAT32_BPB *bpb) {
    memset(bpb, 0, sizeof(FAT32_BPB));
    bpb->bytes_per_sector = bs->bytes_per_sector;
    bpb->sectors_per_cluster = bs->sectors_per_cluster;
    bpb->reserved_sectors = bs->reserved_sectors;
    bpb->num_fats = bs->num_fats;
    bpb->fat_size_sectors = bs->fat_size32;
    bpb->root_cluster = bs->root_cluster;
    bpb->total_sectors = bs->total_sectors16 ? bs->total_sectors16 : bs->total_sectors32;
}

static int is_fat32_partition(uint8_t type) {
    return type == 0x0B || type == 0x0C || type == 0x1B || type == 0x1C;
}

static int fat32_init(BIOS_FAT32 *fs, const BIOS_BOOT_PARAMS *params) {
    memset(fs, 0, sizeof(*fs));
    fs->boot_drive = params->boot_drive;

    if (read_absolute(fs->boot_drive, 0, g_sector) != 0) return -1;

    uint64_t partition_lba = 0;
    MBR_PARTITION *parts = (MBR_PARTITION *)(g_sector + 0x1BE);
    for (int i = 0; i < 4; ++i) {
        if (is_fat32_partition(parts[i].sys_id) && parts[i].start_lba != 0) {
            partition_lba = parts[i].start_lba;
            break;
        }
    }

    if (partition_lba == 0) {
        partition_lba = 2048; // Fallback for ImplusOS BIOS image layout
    }

    fs->partition_lba = partition_lba;
    if (read_absolute(fs->boot_drive, fs->partition_lba, g_sector) != 0) return -1;
    FAT32_BOOT_SECTOR *bs = (FAT32_BOOT_SECTOR *)g_sector;
    if (bs->bytes_per_sector != SECTOR_SIZE || bs->sectors_per_cluster == 0 || bs->fat_size32 == 0) {
        return -1;
    }

    bpb_from_sector(bs, &fs->bpb);
    fs->first_data_sector = fs->bpb.reserved_sectors + (fs->bpb.num_fats * fs->bpb.fat_size_sectors);
    fs->bytes_per_cluster = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;
    return 0;
}

static uint32_t cluster_first_sector(BIOS_FAT32 *fs, uint32_t cluster) {
    return fs->first_data_sector + ((cluster - 2u) * fs->bpb.sectors_per_cluster);
}

static int fat32_next_cluster(BIOS_FAT32 *fs, uint32_t cluster, uint32_t *next) {
    uint32_t fat_offset = cluster * 4u;
    uint32_t sector = fs->bpb.reserved_sectors + (fat_offset / fs->bpb.bytes_per_sector);
    uint32_t offset = fat_offset % fs->bpb.bytes_per_sector;
    if (read_sector(fs, sector, g_sector) != 0) return -1;
    *next = (*(uint32_t *)(g_sector + offset)) & 0x0FFFFFFFu;
    return 0;
}

static uint32_t entry_cluster(const FAT32_DIR_ENTRY *entry) {
    return ((uint32_t)entry->FirstClusterHigh << 16) | entry->FirstClusterLow;
}

static int path_component_equal(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

static void AppendString(char *dst, const char *src) {
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = 0;
}


static void lfn_copy_chars(char *lfn, int order, const FAT32_LFN_ENTRY *e) {
    int base = (order - 1) * 13;
    int pos = base;
    for (int i = 0; i < 13; ++i) {
        uint16_t ch;
        if (i < 5) ch = e->name1[i];
        else if (i < 11) ch = e->name2[i - 5];
        else ch = e->name3[i - 11];
        if (ch == 0x0000 || ch == 0xFFFF) return;
        if (pos < 255) lfn[pos++] = (ch < 0x80) ? (char)ch : '?';
    }
}

static void short_name_to_string(const FAT32_DIR_ENTRY *entry, char *out) {
    int p = 0;
    for (int i = 0; i < 8 && entry->Name[i] != ' '; ++i) out[p++] = entry->Name[i];
    if (entry->Name[8] != ' ') {
        out[p++] = '.';
        for (int i = 8; i < 11 && entry->Name[i] != ' '; ++i) out[p++] = entry->Name[i];
    }
    out[p] = 0;
}

static int find_in_directory(BIOS_FAT32 *fs, uint32_t dir_cluster, const char *name, FAT32_DIR_ENTRY *out) {
    uint32_t cluster = dir_cluster;
    char lfn[256];
    memset(lfn, 0, sizeof(lfn));

    while (cluster < 0x0FFFFFF8u) {
        uint32_t first = cluster_first_sector(fs, cluster);
        for (uint32_t s = 0; s < fs->bpb.sectors_per_cluster; ++s) {
            if (read_sector(fs, first + s, g_sector) != 0) return -1;
            FAT32_DIR_ENTRY *entries = (FAT32_DIR_ENTRY *)g_sector;
            for (uint32_t i = 0; i < SECTOR_SIZE / sizeof(FAT32_DIR_ENTRY); ++i) {
                FAT32_DIR_ENTRY *entry = &entries[i];
                if ((uint8_t)entry->Name[0] == 0x00) return -1;
                if ((uint8_t)entry->Name[0] == 0xE5) {
                    memset(lfn, 0, sizeof(lfn));
                    continue;
                }
                if (entry->Attr == 0x0F) {
                    FAT32_LFN_ENTRY *lfn_entry = (FAT32_LFN_ENTRY *)entry;
                    int order = lfn_entry->order & 0x1F;
                    if (lfn_entry->order & 0x40) memset(lfn, 0, sizeof(lfn));
                    if (order > 0) lfn_copy_chars(lfn, order, lfn_entry);
                    continue;
                }

                char short_name[16];
                short_name_to_string(entry, short_name);
                if ((lfn[0] && path_component_equal(lfn, name)) ||
                    path_component_equal(short_name, name)) {
                    memcpy(out, entry, sizeof(*out));
                    return 0;
                }
                memset(lfn, 0, sizeof(lfn));
            }
        }
        if (fat32_next_cluster(fs, cluster, &cluster) != 0) return -1;
    }
    return -1;
}

static int fat32_find_path(BIOS_FAT32 *fs, const char *path, FAT32_DIR_ENTRY *out) {
    uint32_t dir = fs->bpb.root_cluster;
    const char *p = path;
    while (*p == '/') ++p;

    while (*p) {
        char component[96];
        int n = 0;
        while (p[n] && p[n] != '/' && n + 1 < (int)sizeof(component)) {
            component[n] = p[n];
            ++n;
        }
        component[n] = 0;
        if (find_in_directory(fs, dir, component, out) != 0) return -1;
        p += n;
        while (*p == '/') ++p;
        if (*p) {
            if ((out->Attr & 0x10u) == 0) return -1;
            dir = entry_cluster(out);
        }
    }
    return 0;
}

typedef void (*fat32_dir_callback)(BIOS_FAT32 *fs, const char *name, FAT32_DIR_ENTRY *entry);
static int fat32_iterate_directory(BIOS_FAT32 *fs, uint32_t dir_cluster, fat32_dir_callback cb) {
    uint32_t cluster = dir_cluster;
    char lfn[256];
    memset(lfn, 0, sizeof(lfn));

    while (cluster < 0x0FFFFFF8u) {
        uint32_t first = cluster_first_sector(fs, cluster);
        for (uint32_t s = 0; s < fs->bpb.sectors_per_cluster; ++s) {
            uint8_t local_sector[SECTOR_SIZE];
            if (read_sector(fs, first + s, local_sector) != 0) return -1;
            FAT32_DIR_ENTRY *entries = (FAT32_DIR_ENTRY *)local_sector;
            for (uint32_t i = 0; i < SECTOR_SIZE / sizeof(FAT32_DIR_ENTRY); ++i) {
                FAT32_DIR_ENTRY *entry = &entries[i];
                if ((uint8_t)entry->Name[0] == 0x00) return 0;
                if ((uint8_t)entry->Name[0] == 0xE5) {
                    memset(lfn, 0, sizeof(lfn));
                    continue;
                }
                if (entry->Attr == 0x0F) {
                    FAT32_LFN_ENTRY *lfn_entry = (FAT32_LFN_ENTRY *)entry;
                    int order = lfn_entry->order & 0x1F;
                    if (lfn_entry->order & 0x40) memset(lfn, 0, sizeof(lfn));
                    if (order > 0) lfn_copy_chars(lfn, order, lfn_entry);
                    continue;
                }

                char short_name[16];
                short_name_to_string(entry, short_name);
                
                bios_puts("  Entry: "); bios_puts(short_name); 
                if (lfn[0]) { bios_puts(" (LFN: "); bios_puts(lfn); bios_puts(")"); }
                bios_puts("\n");

                cb(fs, lfn[0] ? lfn : short_name, entry);
                memset(lfn, 0, sizeof(lfn));
            }
        }
        if (fat32_next_cluster(fs, cluster, &cluster) != 0) return -1;
    }
    return 0;
}


static int fat32_read_entry_to(BIOS_FAT32 *fs, FAT32_DIR_ENTRY *entry, void *dest, uint32_t *size_out) {
    uint8_t *dst = (uint8_t *)dest;
    uint32_t remaining = entry->FileSize;
    uint32_t cluster = entry_cluster(entry);
    while (remaining > 0 && cluster < 0x0FFFFFF8u) {
        uint32_t first = cluster_first_sector(fs, cluster);
        for (uint32_t s = 0; s < fs->bpb.sectors_per_cluster && remaining > 0; ++s) {
            if (read_sector(fs, first + s, g_sector) != 0) return -1;
            uint32_t copy = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
            memcpy(dst, g_sector, copy);
            dst += copy;
            remaining -= copy;
        }
        if (remaining > 0 && fat32_next_cluster(fs, cluster, &cluster) != 0) return -1;
    }
    if (size_out) *size_out = entry->FileSize;
    return 0;
}

static int fat32_read_file_to(BIOS_FAT32 *fs, const char *path, void *dest, uint32_t *size_out) {
    FAT32_DIR_ENTRY entry;
    if (fat32_find_path(fs, path, &entry) != 0) return -1;
    if (entry.Attr & 0x10u) return -1;
    return fat32_read_entry_to(fs, &entry, dest, size_out);
}

static int load_kernel_elf(void *image, uint32_t image_size, uint32_t *entry_out) {
    Elf64_Ehdr *eh = (Elf64_Ehdr *)image;
    if (image_size < sizeof(*eh) || eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') return -1;
    if (eh->e_phoff + ((uint64_t)eh->e_phnum * sizeof(Elf64_Phdr)) > image_size) return -1;

    Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)image + eh->e_phoff);
    uint64_t min = UINT64_MAX;
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_LOAD && ph[i].p_paddr < min) min = ph[i].p_paddr;
    }
    if (min == UINT64_MAX) return -1;

    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_offset + ph[i].p_filesz > image_size || ph[i].p_memsz < ph[i].p_filesz) return -1;
        memcpy((void *)(uintptr_t)ph[i].p_paddr, (uint8_t *)image + ph[i].p_offset, (size_t)ph[i].p_filesz);
        memset((void *)(uintptr_t)(ph[i].p_paddr + ph[i].p_filesz), 0, (size_t)(ph[i].p_memsz - ph[i].p_filesz));
    }

    if (eh->e_type == ET_DYN && eh->e_shoff && eh->e_shnum) {
        Elf64_Shdr *sh = (Elf64_Shdr *)((uint8_t *)image + eh->e_shoff);
        for (uint16_t i = 0; i < eh->e_shnum; ++i) {
            if (sh[i].sh_type != SHT_RELA) continue;
            Elf64_Rela *rela = (Elf64_Rela *)((uint8_t *)image + sh[i].sh_offset);
            uint64_t rela_offset = 0;
            while (rela_offset + sizeof(Elf64_Rela) <= sh[i].sh_size) {
                Elf64_Rela *cur = (Elf64_Rela *)((uint8_t *)rela + (uint32_t)rela_offset);
                uint32_t type = (uint32_t)(cur->r_info & 0xFFFFFFFFu);
                if (type == R_X86_64_RELATIVE) {
                    uint64_t *target = (uint64_t *)(uintptr_t)cur->r_offset;
                    *target = (uint64_t)cur->r_addend;
                }
                rela_offset += sizeof(Elf64_Rela);
            }
        }
    }

    *entry_out = (uint32_t)eh->e_entry;
    return 0;
}

static void build_memory_map(const BIOS_BOOT_PARAMS *params, BOOT_INFO *bi) {
    E820_ENTRY *e820 = (E820_ENTRY *)(uintptr_t)(uint32_t)params->e820_map;
    uint32_t count = params->e820_count;
    if (count > 128) count = 128;
    uint32_t out = 0;
    for (uint32_t i = 0; i < count && out < 128; ++i) {
        uint64_t base = e820[i].base;
        uint64_t length = e820[i].length;
        if (base >= 0x100000000ULL) continue;
        if (base + length > 0x100000000ULL) {
            length = 0x100000000ULL - base;
        }
        if (length < 4096u) continue;

        g_memory_map[out].Type = (e820[i].type == 1) ? EFI_CONVENTIONAL_MEMORY : EFI_RESERVED_MEMORY_TYPE;
        g_memory_map[out].Pad = 0;
        g_memory_map[out].PhysicalStart = base;
        g_memory_map[out].VirtualStart = 0;
        g_memory_map[out].NumberOfPages = length >> 12;
        g_memory_map[out].Attribute = 0;
        ++out;
    }
    bi->MemoryMap = (uint64_t)(uintptr_t)g_memory_map;
    bi->MemoryMapSize = out * sizeof(EFI_MEMORY_DESCRIPTOR);
    bi->MemoryMapDescriptorSize = sizeof(EFI_MEMORY_DESCRIPTOR);
    bi->MemoryMapDescriptorVersion = 1;
}

static void on_driver_found(BIOS_FAT32 *fs, const char *name, FAT32_DIR_ENTRY *entry) {
    if (entry->Attr & 0x10u) return; // directory
    int len = 0; while (name[len]) len++;
    if (len < 4) return;
    if (!(name[len-4] == '.' && (name[len-3] == 'E' || name[len-3] == 'e') &&
          (name[len-2] == 'L' || name[len-2] == 'l') && (name[len-1] == 'F' || name[len-1] == 'f'))) {
        return;
    }
    
    bios_puts("[BIOS BootManager] Found driver: ");
    bios_puts(name);
    bios_puts("\n");
    if (g_boot_info.LoadedFileCount >= MAX_LOADED_FILES) return;
    
    uint32_t size = entry->FileSize;
    void *buffer = bios_malloc(size);
    if (!buffer) return;
    
    if (fat32_read_entry_to(fs, entry, buffer, NULL) == 0) {
        UINTN idx = g_boot_info.LoadedFileCount++;
        int p = 0;
        for (; name[p] && p < LOADED_FILE_NAME_MAX - 1; p++) {
            g_boot_info.LoadedFiles[idx].Name[p] = name[p];
        }
        g_boot_info.LoadedFiles[idx].Name[p] = 0;
        g_boot_info.LoadedFiles[idx].PhysAddr = (EFI_PHYSICAL_ADDRESS)(uintptr_t)buffer;
        g_boot_info.LoadedFiles[idx].Size = size;
    }
}

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMP_FILE_HEADER;

typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMP_INFO_HEADER;
#pragma pack(pop)

static inline uint32_t AlphaBlend(uint32_t dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8)  & 0xFF;
    uint8_t db = (dst >> 0)  & 0xFF;
    uint8_t nr = (uint8_t)((r * a + dr * (255 - a)) / 255);
    uint8_t ng = (uint8_t)((g * a + dg * (255 - a)) / 255);
    uint8_t nb = (uint8_t)((b * a + db * (255 - a)) / 255);
    return (0xFF << 24) | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb;
}

static void FillScreen(uint32_t Color) {
    if (!g_boot_info.FrameBufferBase) return;
    uint32_t *FrameBuffer = (uint32_t *)(uintptr_t)g_boot_info.FrameBufferBase;
    uint32_t Width = g_boot_info.HorizontalResolution;
    uint32_t Height = g_boot_info.VerticalResolution;
    uint32_t Pitch = g_boot_info.PixelsPerScanLine;
    for (uint32_t y = 0; y < Height; y++)
        for (uint32_t x = 0; x < Width; x++)
            FrameBuffer[y * Pitch + x] = Color;
}

static void DisplayBMP(BIOS_FAT32 *fs) {
    if (!g_boot_info.FrameBufferBase) return;
    
    FAT32_DIR_ENTRY entry;
    if (fat32_find_path(fs, "/BootManager/Resource/Images/BootLogo.bmp", &entry) != 0) return;
    
    uint32_t size = entry.FileSize;
    void *Buffer = bios_malloc(size);
    if (!Buffer) return;
    
    if (fat32_read_entry_to(fs, &entry, Buffer, NULL) != 0) return;
    
    BMP_FILE_HEADER *FileHdr = (BMP_FILE_HEADER *)Buffer;
    BMP_INFO_HEADER *InfoHdr = (BMP_INFO_HEADER *)((uint8_t *)Buffer + sizeof(BMP_FILE_HEADER));

    if (FileHdr->bfType != 0x4D42) return;

    uint8_t *PixelData = (uint8_t *)Buffer + FileHdr->bfOffBits;
    uint32_t width = (uint32_t)InfoHdr->biWidth;
    int32_t heightSigned = InfoHdr->biHeight;
    uint32_t height = (heightSigned > 0) ? (uint32_t)heightSigned : (uint32_t)(-heightSigned);
    int TopDown = (heightSigned < 0);
    uint32_t bpp = InfoHdr->biBitCount;

    if (bpp != 24 && bpp != 32) return;

    uint32_t ScreenWidth = g_boot_info.HorizontalResolution;
    uint32_t ScreenHeight = g_boot_info.VerticalResolution;
    uint32_t StartX = (ScreenWidth > width) ? (ScreenWidth - width) / 2 : 0;
    uint32_t StartY = (ScreenHeight > height) ? (ScreenHeight - height) / 2 : 0;
    uint32_t RowSize = ((width * (bpp / 8) + 3) & ~3u);

    uint32_t *FrameBuffer = (uint32_t *)(uintptr_t)g_boot_info.FrameBufferBase;
    uint32_t Pitch = g_boot_info.PixelsPerScanLine;

    for (uint32_t y = 0; y < height; y++) {
        uint32_t srcY = TopDown ? y : (height - 1 - y);
        uint8_t *Row = PixelData + srcY * RowSize;
        for (uint32_t x = 0; x < width; x++) {
            uint8_t b_val = Row[x * (bpp / 8) + 0];
            uint8_t g_val = Row[x * (bpp / 8) + 1];
            uint8_t r_val = Row[x * (bpp / 8) + 2];
            uint8_t a_val = (bpp == 32) ? Row[x * (bpp / 8) + 3] : 0xFF;
            if (a_val == 0) continue;

            uint32_t dstColor = FrameBuffer[(StartY + y) * Pitch + (StartX + x)];
            FrameBuffer[(StartY + y) * Pitch + (StartX + x)] = AlphaBlend(dstColor, r_val, g_val, b_val, a_val);
        }
    }
}

static void DrawTextGraySmallCenterBottom(const char *Text, void *FontBuffer, uint32_t FontSize) {
    if (!FontBuffer || FontSize == 0 || !g_boot_info.FrameBufferBase) return;

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, (unsigned char *)FontBuffer, stbtt_GetFontOffsetForIndex((unsigned char *)FontBuffer, 0))) {
        return;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, 26.0f);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    ascent = (int)(ascent * scale);

    int text_width = 0;
    for (int i = 0; Text[i]; i++) {
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, Text[i], &advance, &lsb);
        text_width += (int)(advance * scale);
        if (Text[i + 1])
            text_width += (int)(stbtt_GetCodepointKernAdvance(&font, Text[i], Text[i + 1]) * scale);
    }

    uint32_t ScreenWidth = g_boot_info.HorizontalResolution;
    uint32_t ScreenHeight = g_boot_info.VerticalResolution;
    uint32_t Pitch = g_boot_info.PixelsPerScanLine;
    uint32_t *FrameBuffer = (uint32_t *)(uintptr_t)g_boot_info.FrameBufferBase;

    int start_x = ((int)ScreenWidth - text_width) / 2;
    int start_y = (int)ScreenHeight - 40 - ascent;
    int x = start_x;

    for (int i = 0; Text[i]; i++) {
        int advance, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&font, Text[i], &advance, &lsb);
        stbtt_GetCodepointBitmapBox(&font, Text[i], scale, scale, &x0, &y0, &x1, &y1);

        int char_width = x1 - x0;
        int char_height = y1 - y0;

        if (char_width > 0 && char_height > 0) {
            uint8_t *bitmap = bios_malloc((size_t)(char_width * char_height));
            if (bitmap) {
                stbtt_MakeCodepointBitmap(&font, bitmap, char_width, char_height, char_width, scale, scale, Text[i]);

                for (int yy = 0; yy < char_height; yy++) {
                    for (int xx = 0; xx < char_width; xx++) {
                        uint8_t alpha = bitmap[yy * char_width + xx];
                        if (alpha == 0) continue;
                        int draw_x = x + x0 + xx;
                        int draw_y = start_y + ascent + y0 + yy;
                        if (draw_x < 0 || draw_x >= (int)ScreenWidth || draw_y < 0 || draw_y >= (int)ScreenHeight) continue;

                        uint32_t dstColor = FrameBuffer[draw_y * (int)Pitch + draw_x];
                        FrameBuffer[draw_y * (int)Pitch + draw_x] = AlphaBlend(dstColor, 160, 160, 160, alpha);
                    }
                }
            }
        }

        x += (int)(advance * scale);
        if (Text[i + 1])
            x += (int)(stbtt_GetCodepointKernAdvance(&font, Text[i], Text[i + 1]) * scale);
    }
}


static void bios_put_hex(uint32_t v) {
    const char *hex = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) {
        char c = hex[(v >> i) & 0xF];
        outb(0x3F8, (uint8_t)c);
    }
}

void bootmanager_bios_main(BIOS_BOOT_PARAMS *params) {
    bios_serial_init();
    bios_puts("[BIOS BootManager] start\n");

    if (!params || params->signature != BIOS_BOOT_PARAMS_SIGNATURE) {
        bios_puts("[BIOS BootManager] invalid handoff\n");
        for (;;) __asm__ volatile("hlt");
    }

    BIOS_FAT32 fs;
    if (fat32_init(&fs, params) != 0) {
        bios_puts("[BIOS BootManager] FAT32 init failed\n");
        for (;;) __asm__ volatile("hlt");
    }

    uint32_t kernel_size = 0;
    if (fat32_read_file_to(&fs, "/Kernel/Kernel_Main.ELF",
            (void *)(uintptr_t)BIOS_KERNEL_ELF_BUFFER, &kernel_size) != 0) {
        bios_puts("[BIOS BootManager] long filename kernel lookup failed, trying 8.3 alias\n");
        if (fat32_read_file_to(&fs, "/Kernel/KERNEL~1.ELF",
                (void *)(uintptr_t)BIOS_KERNEL_ELF_BUFFER, &kernel_size) != 0) {
            bios_puts("[BIOS BootManager] Kernel read failed\n");
            for (;;) __asm__ volatile("hlt");
        }
    }

    uint32_t entry = 0;
    if (load_kernel_elf((void *)(uintptr_t)BIOS_KERNEL_ELF_BUFFER, kernel_size, &entry) != 0) {
        bios_puts("[BIOS BootManager] Kernel ELF failed\n");
        for (;;) __asm__ volatile("hlt");
    }

    bios_puts("[BIOS] params ptr: ");
    bios_put_hex((uint32_t)params);
    bios_puts(" sig: ");
    bios_put_hex(params->signature);
    bios_puts(" e820: ");
    bios_put_hex(params->e820_count);
    bios_puts("\n");

    memset(&g_boot_info, 0, sizeof(g_boot_info));
    build_memory_map(params, &g_boot_info);
    g_boot_info.FrameBufferBase = params->framebuffer_base;
    g_boot_info.FrameBufferSize = params->framebuffer_size;
    g_boot_info.HorizontalResolution = params->horizontal_resolution;
    g_boot_info.VerticalResolution = params->vertical_resolution;
    g_boot_info.PixelsPerScanLine = params->pixels_per_scan_line;
    g_boot_info.PartitionStartLBA = fs.partition_lba;
    g_boot_info.BootPartitionBPBValid = 1;
    memcpy(&g_boot_info.BootPartitionBPB, &fs.bpb, sizeof(FAT32_BPB));
    g_boot_info.AcpiRsdpAddress = params->acpi_rsdp;
    g_boot_info.AcpiRsdpSize = params->acpi_rsdp ? 20 : 0;
    g_boot_info.AcpiRsdpRevision = 0;
    g_boot_info.BootDriveType = BOOT_DRIVE_TYPE_IDE;

    FillScreen(0x000000);
    DisplayBMP(&fs);

    char DisplayText[256] = {0};
    AppendString(DisplayText, "CPU: Unknown | Maker: Unknown | Model: Unknown");

    void *font_buffer = NULL;
    uint32_t font_size = 0;
    FAT32_DIR_ENTRY font_entry;
    if (fat32_find_path(&fs, "/BootManager/Resource/Fonts/NotoSansJP-Regular.ttf", &font_entry) == 0) {
        font_size = font_entry.FileSize;
        font_buffer = bios_malloc(font_size);
        if (font_buffer) {
            fat32_read_entry_to(&fs, &font_entry, font_buffer, NULL);
            g_boot_info.FontDataAddress = (uint64_t)(uintptr_t)font_buffer;
            g_boot_info.FontDataSize = font_size;
            
            DrawTextGraySmallCenterBottom(DisplayText, font_buffer, font_size);
        }
    }

    FAT32_DIR_ENTRY dir_entry;
    if (fat32_find_path(&fs, "/Kernel/Driver", &dir_entry) == 0) {
        bios_puts("[BIOS BootManager] Found /Kernel/Driver\n");
        if (dir_entry.Attr & 0x10u) {
            fat32_iterate_directory(&fs, entry_cluster(&dir_entry), on_driver_found);
        } else {
            bios_puts("[BIOS BootManager] /Kernel/Driver is NOT a directory\n");
        }
    } else {
        bios_puts("[BIOS BootManager] /Kernel/Driver NOT found\n");
    }

    bios_puts("[BIOS BootManager] enter kernel\n");
    bios_enter_kernel64(entry, (uint32_t)(uintptr_t)&g_boot_info);
}
