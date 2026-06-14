#include "WM_EventQueue.h"

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

bool wm_event_queue_pop(wm_event_queue_t *queue, ipc_message_t *message)
{
    if (!queue || !message || queue->count == 0u) return false;
    *message = queue->messages[queue->tail];
    queue->tail = (queue->tail + 1u) % WM_EVENT_QUEUE_SIZE;
    --queue->count;
    return true;
}
