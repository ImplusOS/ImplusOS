#include "SDL_internal.h"

#include <SDL3/SDL_gamepad.h>

SDL_GamepadType SDL_GetGamepadTypeFromVIDPID(Uint16 vendor, Uint16 product,
                                             const char *name, bool forUI)
{
    (void)vendor;
    (void)product;
    (void)name;
    (void)forUI;
    return SDL_GAMEPAD_TYPE_STANDARD;
}
