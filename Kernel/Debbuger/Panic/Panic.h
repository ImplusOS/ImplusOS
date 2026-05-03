#ifndef IMPLUSOS_PANIC_H
#define IMPLUSOS_PANIC_H

#include "../../Kernel_Main.h"

void kernel_panic(const char* module_name, const char* message);

#endif