#include <stdint.h>
#include "Syscalls.h"
#include "Process.h"

void _start(void)
{
    while (1) {
        process_yield();
    }
}
