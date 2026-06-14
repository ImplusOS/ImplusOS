#pragma once

#include "../Core/WM_State.h"

void wm_input_handle_keyboard(wm_state_t *state, const ipc_message_t *message);
void wm_input_handle_mouse(wm_state_t *state, const ipc_message_t *message);
