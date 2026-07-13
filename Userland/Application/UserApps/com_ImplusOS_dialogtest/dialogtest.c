#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Graphics.h"
#include "../../../API/Window.h"
#include "dialogtest.h"

void dialogtest_main(void)
{
    window_id_t win = window_create(320, 280, "Popup Dialog Test");
    if (win == 0) return;

    window_load_layout(win, "/Userland/UserApps/com_ImplusOS_dialogtest/Resource/layout.xml");

    window_subscribe_mouse(win);
    window_subscribe_keyboard(win);
    window_show(win);

    while (1) {
        process_yield();

        input_mouse_event_t mouse;
        if (window_input_mouse_poll(&mouse) > 0 && (mouse.buttons & 1)) {
            uint32_t mx = mouse.x;
            uint32_t my = mouse.y;
            if (my >= 44 && my < 88 && mx >= 20 && mx < 220) {
                window_show_info("Information", "This is an informational dialog.\nThe system is running normally.");
            } else if (my >= 104 && my < 148 && mx >= 20 && mx < 220) {
                window_show_warning("Warning", "Low disk space on system partition.\nPlease free up some space.");
            } else if (my >= 164 && my < 208 && mx >= 20 && mx < 220) {
                window_show_error("Critical Error", "An unexpected error occurred.\nThe application will now terminate.");
            }
        }
    }
}

void _start(void)
{
    dialogtest_main();
    while (1) {
        process_yield();
    }
}
