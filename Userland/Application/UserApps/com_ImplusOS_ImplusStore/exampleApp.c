#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Window.h"
#include "exampleApp.h"

void exampleApp_main(void)
{
    window_id_t win = window_create(580, 400, "Implus Store");
    if (win == 0) return;
    
    window_load_layout(win, "/Userland/UserApps/com_ImplusOS_ImplusStore/Resource/layout.xml");
    
    window_subscribe_keyboard(win);
}

void _start(void)
{
    exampleApp_main();
    while (1) {
        process_yield();
    }
}