#include <stdint.h>
#include "API/Process.h"
#include "API/Serial.h"
#include "API/Graphics.h"

static int32_t spawn_with_fallbacks(const char *const *paths, uint32_t path_count) {
    if (paths == 0 || path_count == 0) {
        return -1;
    }

    for (uint32_t i = 0; i < path_count; ++i) {
        const char *path = paths[i];
        if (path == 0 || path[0] == '\0') {
            continue;
        }

        int32_t pid = process_spawn(path);
        if (pid >= 0) {
            return pid;
        }
    }
    return -1;
}

void _start(void)
{
    draw_fill_rect(0, 0, get_display_width(), get_display_height(), 0x000000);
    draw_present();

    static const char *const com_ImplusOS_mousemanager[] = {
        "Userland/SystemApps/com_ImplusOS_mousemanager/com_ImplusOS_mousemanager.ELF",
    };

    static const char *const com_ImplusOS_windowmanager[] = {
        "Userland/SystemApps/com_ImplusOS_windowmanager/com_ImplusOS_windowmanager.ELF",
    };

    static const char *const com_ImplusOS_shell[] = {
        "Userland/SystemApps/com_ImplusOS_shell/com_ImplusOS_shell.ELF",
    };

    static const char *const com_ImplusOS_system[] = {
        "Userland/SystemApps/com_ImplusOS_system/com_ImplusOS_system.ELF",
    };

    static const char *const com_ImplusOS_clock[] = {
        "Userland/UserApps/com_ImplusOS_clock/com_ImplusOS_clock.ELF",
    };

    static const char *const com_ImplusOS_editor[] = {
        "Userland/UserApps/com_ImplusOS_editor/com_ImplusOS_editor.ELF",
    };

    static const char *const com_ImplusOS_exampleApp[] = {
        "Userland/UserApps/com_ImplusOS_exampleApp/com_ImplusOS_exampleApp.ELF",
    };

    static const char *const com_ImplusOS_filemanager[] = {
        "Userland/UserApps/com_ImplusOS_filemanager/com_ImplusOS_filemanager.ELF",
    };

        static const char *const netsurf[] = {
        "Userland/UserApps/netsurf/netsurf.ELF",
    };

    static const char *const com_ImplusOS_vm[] = {
        "Userland/UserApps/com_ImplusOS_vm/com_ImplusOS_vm.ELF",
    };

    spawn_with_fallbacks(com_ImplusOS_mousemanager, sizeof(com_ImplusOS_mousemanager) / sizeof(com_ImplusOS_mousemanager[0]));
    for (uint64_t i = 0; i < 10; i++) {
        process_yield();
    }
    spawn_with_fallbacks(com_ImplusOS_windowmanager, sizeof(com_ImplusOS_windowmanager) / sizeof(com_ImplusOS_windowmanager[0]));
    for (uint64_t i = 0; i < 10; i++) {
        process_yield();
    }
    spawn_with_fallbacks(com_ImplusOS_shell, sizeof(com_ImplusOS_shell) / sizeof(com_ImplusOS_shell[0]));
    for (uint64_t i = 0; i < 10; i++) {
        process_yield();
    }
    spawn_with_fallbacks(com_ImplusOS_system, sizeof(com_ImplusOS_system) / sizeof(com_ImplusOS_system[0]));
    for (uint64_t i = 0; i < 10; i++) {
        process_yield();
    }
    spawn_with_fallbacks(com_ImplusOS_editor, sizeof(com_ImplusOS_editor) / sizeof(com_ImplusOS_editor[0]));
    for (uint64_t i = 0; i < 10; i++) {
        process_yield();
    }
    spawn_with_fallbacks(com_ImplusOS_exampleApp, sizeof(com_ImplusOS_exampleApp) / sizeof(com_ImplusOS_exampleApp[0]));
    for (uint64_t i = 0; i < 10; i++) {
        process_yield();
    }
    spawn_with_fallbacks(com_ImplusOS_filemanager, sizeof(com_ImplusOS_filemanager) / sizeof(com_ImplusOS_filemanager[0]));
    for (uint64_t i = 0; i < 10; i++) {
        process_yield();
    }
    spawn_with_fallbacks(netsurf, sizeof(netsurf) / sizeof(netsurf[0]));
    for (uint64_t i = 0; i < 10; i++) {
        process_yield();
    }
    spawn_with_fallbacks(com_ImplusOS_vm, sizeof(com_ImplusOS_vm) / sizeof(com_ImplusOS_vm[0]));
    for (uint64_t i = 0; i < 10; i++) {
        process_yield();
    }
    //spawn_with_fallbacks(com_ImplusOS_clock, sizeof(com_ImplusOS_clock) / sizeof(com_ImplusOS_clock[0]));
    for (uint64_t i = 0; i < 10; i++) {
        process_yield();
    }

    while (1) {
        process_yield();
    }
}

