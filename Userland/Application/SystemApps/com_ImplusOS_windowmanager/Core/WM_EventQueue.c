#include "WM_EventQueue.h"
#include "../../../../../Userland/API/WM_Protocol.h"

#include <string.h>

void wm_event_queue_init(wm_event_queue_t *queue)
{
    if (queue) memset(queue, 0, sizeof(*queue));
}

bool wm_event_queue_push(wm_event_queue_t *queue, const ipc_message_t *message)
{
    if (!queue || !message) return false;
    if (queue->count == WM_EVENT_QUEUE_SIZE) {
        ++queue->dropped;
        return false;
    }
    queue->messages[queue->head] = *message;
    queue->head = (queue->head + 1u) % WM_EVENT_QUEUE_SIZE;
    ++queue->count;
    return true;
}

static bool message_has_type(const ipc_message_t *message, uint32_t type)
{
    if (!message || message->size < sizeof(wm_msg_header_t)) return false;
    const wm_msg_header_t *header = (const wm_msg_header_t *)message->data;
    return header->type == type;
}

bool wm_event_queue_push_coalesced(wm_event_queue_t *queue,
                                   const ipc_message_t *message)
{
    if (!queue || !message) return false;
    if (message_has_type(message, WM_MOUSE_EVENT)) {
        for (uint32_t i = 0; i < queue->count; ++i) {
            uint32_t index = (queue->tail + i) % WM_EVENT_QUEUE_SIZE;
            if (message_has_type(&queue->messages[index], WM_MOUSE_EVENT)) {
                queue->messages[index] = *message;
                return true;
            }
        }
    }
    return wm_event_queue_push(queue, message);
}

bool wm_event_queue_pop(wm_event_queue_t *queue, ipc_message_t *message)
{
    if (!queue || !message || queue->count == 0u) return false;
    *message = queue->messages[queue->tail];
    queue->tail = (queue->tail + 1u) % WM_EVENT_QUEUE_SIZE;
    --queue->count;
    return true;
}
