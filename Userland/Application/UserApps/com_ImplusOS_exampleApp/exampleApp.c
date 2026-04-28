#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Graphics.h"
#include "../../../API/Window.h"
#include "exampleApp.h"

void exampleApp_main(void)
{
    window_id_t win = window_create(500, 400, "Welcome To ImplusOS ! | Demo Application.");
    if (win == 0) return;
    
    window_load_layout(win, "/Userland/UserApps/com_ImplusOS_exampleApp/Resource/layout.xml");
    
    window_subscribe_keyboard(win);
}

void _start(void)
{
    exampleApp_main();
    while (1) {
        process_yield();
    }
}