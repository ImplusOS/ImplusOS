#include "../../../API/Window.h"
#include "../../../API/Graphics.h"
#include "../../../API/Input.h"
#include "../../../API/Process.h"
#include <string.h>

void _start(void)
{
    window_id_t wid = window_create_ex(0, 0, 1, 1, 0x00000000, "MouseManager");
    if (wid != 0) {
        window_set_system(wid, true);
        window_hide(wid); 
    }
    
    while (1) {
        process_yield();
    }
}
