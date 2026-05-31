#include "../Userland/API/File.h"
#include "../Userland/API/Graphics.h"
#include "../Userland/API/Input.h"
#include "../Userland/API/Process.h"
#include "../Userland/API/Serial.h"
#include "../Userland/API/SystemInfo.h"
#include "../libc/include/string.h"
#include "../libc/include/stdlib.h"
#include "../libc/include/math.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc(x,u)  ((void)(u),malloc(x))
#define STBTT_free(x,u)    ((void)(u),free(x))
#define STBTT_fmod(x,y)    fmod(x,y)
#include "../Thirdparty/stb_truetype.h"

#define RECOVERY_PAYLOAD_PATH "/Recovery/ImplusOS-root.tar.gz"
#define RECOVERY_INSTALL_IMAGE_PATH "/Recovery/ImplusOS-install.img"
#define RECOVERY_MANIFEST_PATH "/Recovery/MANIFEST.txt"
#define RECOVERY_FONT_PATH "/BootManager/Resource/Fonts/NotoSansJP-Regular.ttf"
#define INSTALL_CHUNK_SECTORS 64u
#define INSTALL_SECTOR_SIZE 512u

#define FONT_SIZE 14
#define FONT_SIZE_LARGE 20
#define MENU_ITEM_HEIGHT 28
#define PROGRESS_BAR_HEIGHT 20

static stbtt_fontinfo g_font_info = {0};
static uint8_t *g_font_data = NULL;
static uint32_t g_font_data_size = 0;
static bool g_font_loaded = false;

static void puts_serial(const char *s)
{
    serial_write_string(s);
}

static void put_u64_serial(uint64_t value)
{
    serial_write_uint64(value);
}

static int init_font(void)
{
    if (g_font_loaded) {
        return 0;
    }

    file_stat_t stat;
    memset(&stat, 0, sizeof(stat));
    if (file_stat(RECOVERY_FONT_PATH, &stat) < 0 || !stat.exists) {
        puts_serial("Warning: Font file not found\n");
        return -1;
    }

    g_font_data = (uint8_t *)malloc(stat.size);
    if (!g_font_data) {
        puts_serial("Error: Failed to allocate memory for font\n");
        return -1;
    }

    int32_t fd = file_open(RECOVERY_FONT_PATH, 0);
    if (fd < 0) {
        puts_serial("Error: Failed to open font file\n");
        free(g_font_data);
        g_font_data = NULL;
        return -1;
    }

    int64_t read_len = file_read(fd, g_font_data, stat.size);
    file_close(fd);

    if (read_len != (int64_t)stat.size) {
        puts_serial("Error: Failed to read font file\n");
        free(g_font_data);
        g_font_data = NULL;
        return -1;
    }

    g_font_data_size = stat.size;

    if (!stbtt_InitFont(&g_font_info, g_font_data, stbtt_GetFontOffsetForIndex(g_font_data, 0))) {
        puts_serial("Error: Failed to initialize font\n");
        free(g_font_data);
        g_font_data = NULL;
        return -1;
    }

    g_font_loaded = true;
    puts_serial("Font loaded successfully\n");
    return 0;
}

static uint32_t blend(uint32_t src, uint32_t dst, uint8_t alpha)
{
    uint32_t sa = alpha;
    uint32_t ia = 255 - sa;

    uint32_t sr = (src >> 16) & 0xFF;
    uint32_t sg = (src >> 8) & 0xFF;
    uint32_t sb = src & 0xFF;

    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t db = dst & 0xFF;

    uint32_t r = (sr * sa + dr * ia) / 255;
    uint32_t g = (sg * sa + dg * ia) / 255;
    uint32_t b = (sb * sa + db * ia) / 255;

    return (r << 16) | (g << 8) | b;
}

static void draw_text_at(int x, int y, const char *text, uint32_t color, int font_size)
{
    if (!g_font_loaded) return;

    uint32_t w = get_display_width();
    uint32_t h = get_display_height();
    if (w == 0 || h == 0) return;

    float scale = stbtt_ScaleForPixelHeight(&g_font_info, (float)font_size);

    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&g_font_info, &ascent, &descent, &linegap);

    int baseline = (int)(ascent * scale);
    int current_x = x;

    const unsigned char *p = (const unsigned char *)text;

    while (*p) {
        if (*p >= 32 && *p < 127) {

            int advance, lsb;
            stbtt_GetCodepointHMetrics(&g_font_info, *p, &advance, &lsb);

            int ix0, iy0, ix1, iy1;
            stbtt_GetCodepointBitmapBox(&g_font_info, *p, scale, scale, &ix0, &iy0, &ix1, &iy1);

            int bw = ix1 - ix0;
            int bh = iy1 - iy0;

            if (bw > 0 && bh > 0) {

                unsigned char *bitmap =
                    stbtt_GetCodepointBitmap(&g_font_info, scale, scale, *p,
                                             &bw, &bh, NULL, NULL);

                if (bitmap) {
                    for (int py = 0; py < bh; py++) {
                        for (int px = 0; px < bw; px++) {

                            int sx = current_x + ix0 + px;
                            int sy = y + baseline + iy0 + py;

                            if (sx < 0 || sy < 0 || sx >= (int)w || sy >= (int)h)
                                continue;

                            uint8_t alpha = bitmap[py * bw + px];

                            if (alpha == 0) continue;

                            uint32_t dst = get_pixel(sx, sy);
                            uint32_t blended = blend(color, dst, alpha);

                            draw_pixel(sx, sy, blended);
                        }
                    }
                    free(bitmap);
                }
            }

            current_x += (int)(advance * scale);
        }
        p++;
    }
}

static void draw_button_rect(int x, int y, int width, int height, uint32_t fill_color, uint32_t border_color, int border_width)
{
    draw_fill_rect(x, y, width, height, fill_color);
    
    for (int i = 0; i < border_width; i++) {
        for (int px = 0; px < width; px++) {
            draw_pixel(x + px, y + i, border_color);
            draw_pixel(x + px, y + height - 1 - i, border_color);
        }
        for (int py = 0; py < height; py++) {
            draw_pixel(x + i, y + py, border_color);
            draw_pixel(x + width - 1 - i, y + py, border_color);
        }
    }
}

static void draw_progress_bar(int x, int y, int width, int height, float progress, uint32_t bg_color, uint32_t fg_color)
{
    draw_fill_rect(x, y, width, height, bg_color);
    if (progress > 0.0f && progress <= 1.0f) {
        int fill_width = (int)((float)width * progress);
        draw_fill_rect(x, y, fill_width, height, fg_color);
    }
    
    for (int i = 0; i < 2; i++) {
        for (int px = 0; px < width; px++) {
            draw_pixel(x + px, y + i, 0xFFFFFFFF);
            draw_pixel(x + px, y + height - 1 - i, 0xFFFFFFFF);
        }
        for (int py = 0; py < height; py++) {
            draw_pixel(x + i, y + py, 0xFFFFFFFF);
            draw_pixel(x + width - 1 - i, y + py, 0xFFFFFFFF);
        }
    }
}

static void draw_status_screen(uint32_t color, const char *title, const char *message)
{
    uint32_t w = get_display_width();
    uint32_t h = get_display_height();
    if (w == 0 || h == 0) {
        return;
    }

    draw_fill_rect(0, 0, w, h, 0xFF101820);

    draw_fill_rect(0, 0, w, 80, color);
    draw_text_at(32, 25, "ImplusOS Recovery", 0xFFFFFFFF, FONT_SIZE_LARGE);

    if (title) {
        draw_text_at(32, 120, title, 0xFFE0E0E0, FONT_SIZE);
    }

    if (message) {
        draw_text_at(32, 180, message, 0xFFA0A0A0, FONT_SIZE);
    }

    draw_fill_rect(0, h - 40, w, 40, 0xFF1B2733);
    draw_text_at(32, h - 25, "Press 'q' to cancel, 'r' to reboot", 0xFFB0B0B0, FONT_SIZE - 2);

    draw_present();
}

static int read_key_ascii(void)
{
    bool key_down[256];
    memset(key_down, 0, sizeof(key_down));

    while (1) {
        input_keyboard_event_t ev;
        if (input_read_keyboard(&ev) < 0) {
            process_yield();
            continue;
        }

        uint8_t key = (uint8_t)ev.keycode;
        if (!ev.pressed) {
            key_down[key] = false;
            continue;
        }

        if (key_down[key]) {
            continue;
        }
        key_down[key] = true;

        if (ev.ascii != 0) {
            return ev.ascii;
        }
    }
}

static void print_disk_info(uint32_t index, const system_disk_info_t *info)
{
    puts_serial("  [");
    serial_write_uint32(index);
    puts_serial("] ");
    puts_serial(info->disk_name);
    puts_serial("  ");
    puts_serial(info->manufacturer);
    puts_serial(" ");
    puts_serial(info->model);
    puts_serial("  bytes=");
    put_u64_serial(info->total_bytes);
    puts_serial("  sector=");
    serial_write_uint32(info->sector_size);
    if (info->flags & SYSTEM_DISK_FLAG_BOOT) {
        puts_serial(" boot");
    }
    if (info->flags & SYSTEM_DISK_FLAG_WRITABLE) {
        puts_serial(" writable");
    }
    puts_serial("\n");
}

static void draw_disk_menu(const system_disk_info_t *disks, uint32_t count, int selected)
{
    uint32_t w = get_display_width();
    uint32_t h = get_display_height();
    if (w == 0 || h == 0) {
        return;
    }

    draw_fill_rect(0, 0, w, h, 0xFF101820);

    draw_fill_rect(0, 0, w, 80, 0xFF2F80ED);
    draw_text_at(32, 25, "Select Installation Target", 0xFFFFFFFF, FONT_SIZE_LARGE);

    int start_y = 120;
    for (uint32_t i = 0; i < count && i < 8; i++) {
        int y = start_y + (int)(i * MENU_ITEM_HEIGHT);
        bool is_selected = (i == (uint32_t)selected);
        
        uint32_t bg_color = is_selected ? 0xFF2F80ED : 0xFF1B2733;
        uint32_t text_color = is_selected ? 0xFFFFFFFF : 0xFFB0B0B0;
        
        draw_fill_rect(32, y, w - 64, MENU_ITEM_HEIGHT - 4, bg_color);

        char num_str[4];
        num_str[0] = (char)('0' + (i % 10));
        num_str[1] = '\0';
        draw_text_at(48, y + 6, num_str, text_color, FONT_SIZE);

        draw_text_at(80, y + 6, disks[i].disk_name, text_color, FONT_SIZE);
        draw_text_at(200, y + 6, disks[i].manufacturer, 0xFF909090, FONT_SIZE - 2);
    }

    draw_fill_rect(0, h - 40, w, 40, 0xFF1B2733);
    draw_text_at(32, h - 25, "Use arrow keys to select, Enter to confirm, 'q' to cancel", 0xFFB0B0B0, FONT_SIZE - 2);

    draw_present();
}

static int choose_disk(uint32_t *out_index)
{
    uint32_t count = 0;
    if (os_get_disk_count(&count) < 0 || count == 0) {
        puts_serial("No installable disks were reported by the kernel.\n");
        draw_status_screen(0xFFEB5757, "Error", "No disks found. Press 'r' to reboot.");
        while (1) {
            int ch = read_key_ascii();
            if (ch == 'r' || ch == 'R') {
                system_reboot();
            }
        }
        return -1;
    }

    puts_serial("Install target disks:\n");
    
    system_disk_info_t *disks = (system_disk_info_t *)malloc(sizeof(system_disk_info_t) * count);
    if (!disks) {
        puts_serial("Memory allocation failed\n");
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        memset(&disks[i], 0, sizeof(disks[i]));
        if (os_get_disk_info(i, &disks[i]) == 0) {
            print_disk_info(i, &disks[i]);
        }
    }

    int selected = 0;
    while (1) {
        draw_disk_menu(disks, count, selected);
        int ch = read_key_ascii();
        
        if (ch == 'q' || ch == 'Q') {
            puts_serial("q\n");
            free(disks);
            return -1;
        }
        
        if (ch >= '0' && ch <= '9') {
            uint32_t index = (uint32_t)(ch - '0');
            if (index < count) {
                puts_serial("\n");
                free(disks);
                *out_index = index;
                return 0;
            }
        }

        if (ch == 258) {
            selected = (selected + 1) % (int)count;
        } else if (ch == 259) {
            selected = (selected - 1 + (int)count) % (int)count;
        } else if (ch == '\r' || ch == '\n') {
            puts_serial("\n");
            free(disks);
            *out_index = (uint32_t)selected;
            return 0;
        }
    }
}

static void show_payload_status(void)
{
    file_stat_t stat;
    memset(&stat, 0, sizeof(stat));
    if (file_stat(RECOVERY_PAYLOAD_PATH, &stat) == 0 && stat.exists) {
        puts_serial("Payload: ");
        puts_serial(RECOVERY_PAYLOAD_PATH);
        puts_serial(" (");
        serial_write_uint32(stat.size);
        puts_serial(" bytes)\n");
    } else {
        puts_serial("Payload is missing: ");
        puts_serial(RECOVERY_PAYLOAD_PATH);
        puts_serial("\n");
    }

    memset(&stat, 0, sizeof(stat));
    if (file_stat(RECOVERY_INSTALL_IMAGE_PATH, &stat) == 0 && stat.exists) {
        puts_serial("Install image: ");
        puts_serial(RECOVERY_INSTALL_IMAGE_PATH);
        puts_serial(" (");
        serial_write_uint32(stat.size);
        puts_serial(" bytes)\n");
    } else {
        puts_serial("Install image is missing: ");
        puts_serial(RECOVERY_INSTALL_IMAGE_PATH);
        puts_serial("\n");
    }

    memset(&stat, 0, sizeof(stat));
    if (file_stat(RECOVERY_MANIFEST_PATH, &stat) == 0 && stat.exists) {
        puts_serial("Manifest: ");
        puts_serial(RECOVERY_MANIFEST_PATH);
        puts_serial("\n");
    }
}

static int confirm_install(void)
{
    uint32_t w = get_display_width();
    uint32_t h = get_display_height();

    while (1) {
        draw_fill_rect(0, 0, w, h, 0xFF101820);
        draw_fill_rect(0, 0, w, 80, 0xFFEB5757);
        draw_text_at(32, 25, "Warning", 0xFFFFFFFF, FONT_SIZE_LARGE);

        draw_text_at(32, 150, "This will overwrite the selected disk.", 0xFFE0E0E0, FONT_SIZE);
        draw_text_at(32, 190, "All data will be lost.", 0xFFE0E0E0, FONT_SIZE);
        draw_text_at(32, 240, "Press 'y' to confirm, 'n' or 'q' to cancel", 0xFFC0C0C0, FONT_SIZE);

        draw_button_rect(100, 320, 120, 50, 0xFF27AE60, 0xFF00FF00, 2);
        draw_text_at(130, 340, "Yes (Y)", 0xFFFFFFFF, FONT_SIZE);

        draw_button_rect(w - 220, 320, 120, 50, 0xFFEB5757, 0xFFFF0000, 2);
        draw_text_at(w - 180, 340, "No (N)", 0xFFFFFFFF, FONT_SIZE);

        draw_present();

        int ch = read_key_ascii();
        puts_serial("\n");

        if (ch == 'Y' || ch == 'y') {
            return 1;
        }
        if (ch == 'N' || ch == 'n' || ch == 'q' || ch == 'Q') {
            return 0;
        }
    }
}

static int install_image_to_disk(uint32_t disk_index)
{
    file_stat_t stat;
    memset(&stat, 0, sizeof(stat));
    if (file_stat(RECOVERY_INSTALL_IMAGE_PATH, &stat) < 0 || !stat.exists || stat.size == 0) {
        puts_serial("Install failed: install image not found.\n");
        draw_status_screen(0xFFEB5757, "Error", "Install image not found. Press 'r' to reboot.");
        while (1) {
            int ch = read_key_ascii();
            if (ch == 'r' || ch == 'R') {
                system_reboot();
            }
        }
        return -1;
    }

    if ((stat.size % INSTALL_SECTOR_SIZE) != 0) {
        puts_serial("Install failed: install image is not sector aligned.\n");
        draw_status_screen(0xFFEB5757, "Error", "Install image is not sector aligned. Press 'r' to reboot.");
        while (1) {
            int ch = read_key_ascii();
            if (ch == 'r' || ch == 'R') {
                system_reboot();
            }
        }
        return -1;
    }

    int32_t fd = file_open(RECOVERY_INSTALL_IMAGE_PATH, 0);
    if (fd < 0) {
        puts_serial("Install failed: cannot open install image.\n");
        draw_status_screen(0xFFEB5757, "Error", "Cannot open install image. Press 'r' to reboot.");
        while (1) {
            int ch = read_key_ascii();
            if (ch == 'r' || ch == 'R') {
                system_reboot();
            }
        }
        return -1;
    }

    static uint8_t buffer[INSTALL_CHUNK_SECTORS * INSTALL_SECTOR_SIZE] __attribute__((aligned(16)));
    uint32_t total_sectors = stat.size / INSTALL_SECTOR_SIZE;
    uint32_t written_sectors = 0;

    puts_serial("Writing install image sectors: ");
    serial_write_uint32(total_sectors);
    puts_serial("\n");

    uint32_t w = get_display_width();
    uint32_t h = get_display_height();

    while (written_sectors < total_sectors) {
        uint32_t sectors_left = total_sectors - written_sectors;
        uint32_t chunk_sectors = sectors_left > INSTALL_CHUNK_SECTORS ? INSTALL_CHUNK_SECTORS : sectors_left;
        uint32_t chunk_bytes = chunk_sectors * INSTALL_SECTOR_SIZE;

        int64_t read_len = file_read(fd, buffer, chunk_bytes);
        if (read_len != (int64_t)chunk_bytes) {
            file_close(fd);
            puts_serial("Install failed: could not read install image.\n");
            draw_status_screen(0xFFEB5757, "Error", "Failed to read install image. Press 'r' to reboot.");
            while (1) {
                int ch = read_key_ascii();
                if (ch == 'r' || ch == 'R') {
                    system_reboot();
                }
            }
            return -1;
        }

        int64_t status = os_raw_block_write(disk_index, written_sectors, buffer, chunk_sectors);
        if (status < 0) {
            file_close(fd);
            puts_serial("Install failed: raw block write failed at sector ");
            serial_write_uint32(written_sectors);
            puts_serial(".\n");
            draw_status_screen(0xFFEB5757, "Error", "Block write failed. Press 'r' to reboot.");
            while (1) {
                int ch = read_key_ascii();
                if (ch == 'r' || ch == 'R') {
                    system_reboot();
                }
            }
            return -1;
        }

        written_sectors += chunk_sectors;

        if ((written_sectors % 256u) == 0 || written_sectors == total_sectors) {
            float progress = (float)written_sectors / (float)total_sectors;

            draw_fill_rect(0, 0, w, h, 0xFF101820);
            draw_fill_rect(0, 0, w, 80, 0xFF2D9CDB);
            draw_text_at(32, 25, "Installing ImplusOS", 0xFFFFFFFF, FONT_SIZE_LARGE);

            draw_text_at(32, 150, "Installation Progress", 0xFFE0E0E0, FONT_SIZE);

            int bar_width = w - 128;
            int bar_x = 64;
            int bar_y = 220;
            draw_progress_bar(bar_x, bar_y, bar_width, PROGRESS_BAR_HEIGHT, progress, 0xFF1B2733, 0xFF27AE60);

            int percent = (int)(progress * 100.0f);
            char percent_str[8];
            percent_str[0] = (char)('0' + (percent / 100) % 10);
            percent_str[1] = (char)('0' + (percent / 10) % 10);
            percent_str[2] = (char)('0' + percent % 10);
            percent_str[3] = '%';
            percent_str[4] = '\0';
            draw_text_at(bar_x + bar_width - 80, bar_y + 4, percent_str, 0xFFFFFFFF, FONT_SIZE);

            draw_present();
        }

        if ((written_sectors % 2048u) == 0 || written_sectors == total_sectors) {
            puts_serial("  written ");
            serial_write_uint32(written_sectors);
            puts_serial("/");
            serial_write_uint32(total_sectors);
            puts_serial(" sectors\n");
        }
    }

    file_close(fd);
    puts_serial("Install complete. Press r to reboot.\n");
    return 0;
}

static void run_recovery(void)
{
    init_font();

    draw_status_screen(0xFF2F80ED, "Recovery Environment", "Initializing...");
    puts_serial("\nImplusOS Recovery Environment\n");
    puts_serial("This media boots the normal ImplusOS kernel with a recovery userland.\n\n");
    show_payload_status();

    uint32_t disk_index = 0;
    if (choose_disk(&disk_index) < 0) {
        draw_status_screen(0xFF7A869A, "Canceled", "Install canceled. Press 'r' to reboot.");
        puts_serial("Install canceled. Press r to reboot.\n");
    } else {
        draw_status_screen(0xFF2D9CDB, "Disk Selected", "Verifying disk configuration...");
        puts_serial("Selected disk ");
        serial_write_uint32(disk_index);
        puts_serial(".\n");

        system_disk_info_t selected_info;
        memset(&selected_info, 0, sizeof(selected_info));
        if (os_get_disk_info(disk_index, &selected_info) < 0) {
            draw_status_screen(0xFFEB5757, "Error", "Selected disk disappeared. Press 'r' to reboot.");
            puts_serial("Install failed: selected disk disappeared. Press r to reboot.\n");
        } else if (selected_info.flags & SYSTEM_DISK_FLAG_BOOT) {
            draw_status_screen(0xFFEB5757, "Error", "Cannot install to boot media. Press 'r' to reboot.");
            puts_serial("Install refused: selected disk is the recovery boot media.\n");
            puts_serial("Attach/select the main disk, then boot this installer again. Press r to reboot.\n");
        } else if ((selected_info.flags & SYSTEM_DISK_FLAG_WRITABLE) == 0) {
            draw_status_screen(0xFFEB5757, "Error", "Selected disk is not writable. Press 'r' to reboot.");
            puts_serial("Install refused: selected disk is not writable. Press r to reboot.\n");
        } else if (confirm_install()) {
            if (install_image_to_disk(disk_index) == 0) {
                draw_status_screen(0xFF27AE60, "Success", "Installation complete! Press 'r' to reboot.");
                puts_serial("Installation successful!\n");
            } else {
                draw_status_screen(0xFFEB5757, "Error", "Installation failed. Press 'r' to reboot.");
                puts_serial("Press r to reboot.\n");
            }
        } else {
            draw_status_screen(0xFF7A869A, "Canceled", "Install canceled. Press 'r' to reboot.");
            puts_serial("Install canceled. Press r to reboot.\n");
        }
    }

    while (1) {
        int ch = read_key_ascii();
        if (ch == 'r' || ch == 'R') {
            system_reboot();
        }
    }
}

void _start(void)
{
    run_recovery();
    while (1) {
        process_yield();
    }
}
