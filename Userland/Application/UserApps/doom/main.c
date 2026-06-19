#include <Process.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "doomgeneric.h"

#define DOOM_DEFAULT_WAD_DIR "/Userland/UserApps/doom/Resource"
#define WAD_DIRECTORY_ENTRY_SIZE 16u

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

static int lump_name_equals(const uint8_t *name, const char *expected)
{
    for (uint32_t i = 0u; i < 8u; ++i) {
        char expected_ch = expected[i];
        uint8_t name_ch = name[i];

        if (expected_ch == '\0') {
            return name_ch == 0u;
        }
        if (name_ch != (uint8_t)expected_ch) {
            return 0;
        }
    }

    return expected[8] == '\0';
}

static int wad_has_required_lumps(const char *path)
{
    uint8_t header[12];
    uint8_t entry[WAD_DIRECTORY_ENTRY_SIZE];
    uint32_t num_lumps;
    uint32_t directory_offset;
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        return 0;
    }

    if (fread(header, 1u, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return 0;
    }

    if (memcmp(header, "IWAD", 4u) != 0 && memcmp(header, "PWAD", 4u) != 0) {
        fclose(file);
        return 0;
    }

    num_lumps = read_le32(&header[4]);
    directory_offset = read_le32(&header[8]);
    if (num_lumps == 0u || num_lumps > 20000u ||
        directory_offset > 0x7fffffffu) {
        fclose(file);
        return 0;
    }

    if (fseek(file, (long)directory_offset, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    int has_stbar = 0;
    int has_playpal = 0;
    int has_titlepic = 0;

    for (uint32_t i = 0u; i < num_lumps; ++i) {
        if (fread(entry, 1u, sizeof(entry), file) != sizeof(entry)) {
            fclose(file);
            return 0;
        }

        if (lump_name_equals(&entry[8], "STBAR")) {
            has_stbar = 1;
        } else if (lump_name_equals(&entry[8], "PLAYPAL")) {
            has_playpal = 1;
        } else if (lump_name_equals(&entry[8], "TITLEPIC")) {
            has_titlepic = 1;
        }

        if (has_stbar && has_playpal && has_titlepic) {
            break;
        }
    }

    fclose(file);
    return has_stbar && has_playpal && has_titlepic;
}

static const char *select_iwad(const char *launch_arg)
{
    static const char *const candidates[] = {
        DOOM_DEFAULT_WAD_DIR "/doom.wad",
        DOOM_DEFAULT_WAD_DIR "/freedoom1.wad",
        DOOM_DEFAULT_WAD_DIR "/doom2.wad",
        DOOM_DEFAULT_WAD_DIR "/freedoom2.wad",
        DOOM_DEFAULT_WAD_DIR "/doom1.wad",
    };

    if (launch_arg != NULL && launch_arg[0] != '\0') {
        if (wad_has_required_lumps(launch_arg)) {
            return launch_arg;
        }
    }

    for (uint32_t i = 0u; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (wad_has_required_lumps(candidates[i])) {
            return candidates[i];
        }
    }

    process_exit(1);
    for (;;) {
        process_yield();
    }
}

int main(void)
{
    static char launch_arg[256];
    const char *iwad_path;
    int32_t launch_len = process_get_launch_argument(launch_arg,
                                                     sizeof(launch_arg));
    if (launch_len <= 0) {
        launch_arg[0] = '\0';
    }

    iwad_path = select_iwad(launch_arg);

    char *argv[] = {
        "doom",
        "-iwad",
        (char *)iwad_path,
        "-gfxmode",
        "rgba8888",
        "-nogui",
        NULL,
    };

    doomgeneric_Create(6, argv);

    for (;;) {
        doomgeneric_Tick();
        process_yield();
    }

    return 0;
}

void _start(void)
{
    int status = main();
    process_exit((int32_t)status);
    for (;;) {
        process_yield();
    }
}
