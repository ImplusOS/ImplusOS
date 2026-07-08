#include "../../../API/Process.h"

extern int main(int argc, char **argv);

typedef void (*init_func_t)(void);

extern init_func_t __preinit_array_start[];
extern init_func_t __preinit_array_end[];
extern init_func_t __init_array_start[];
extern init_func_t __init_array_end[];
extern init_func_t __ctors_start[];
extern init_func_t __ctors_end[];

/*
 * libnsfb surface registration functions.
 *
 * These are normally invoked via __attribute__((constructor)), but that
 * mechanism relies on the compiler populating the .ctors (or .init_array)
 * section with a function pointer.  When compiling with -mcmodel=large the
 * compiler emits the register function into .text.startup but does NOT place
 * a pointer in .ctors, so the run_init_range loop below finds an empty table
 * and the surfaces are never registered.  The symptom is:
 *
 *   netsurf: Unknown surface `sdl`
 *   netsurf: Valid surface names are:        <- empty list
 *   unable to process command line.
 *
 * Work-around: call each surface registration function explicitly before
 * entering main().
 */
extern void sdl_register_surface(void);
extern void ram_register_surface(void);

static void run_init_range(init_func_t *start, init_func_t *end)
{
    for (init_func_t *fn = start; fn < end; ++fn) {
        if (*fn && *fn != (init_func_t)-1) {
            (*fn)();
        }
    }
}

void _start(void)
{
    static char *argv[] = {
        "netsurf",
        "-f",
        "sdl",
        "-w",
        "1024",
        "-h",
        "720",
        "http://example.com/",
        0
    };

    /* Register libnsfb surfaces explicitly (see comment above). */
    sdl_register_surface();
    ram_register_surface();

    run_init_range(__preinit_array_start, __preinit_array_end);
    run_init_range(__init_array_start, __init_array_end);
    run_init_range(__ctors_start, __ctors_end);

    int status = main(8, argv);
    process_exit(status);
    for (;;) {
        sleep_ms(1000u);
    }
}
