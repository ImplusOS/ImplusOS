#include "Syscall_Socket.h"

#include "Core/process/ProcessManager.h"
#include "Core/sync/Spinlock.h"
#include "Network/tcp/TCP.h"
#include "Core/timer/Timer.h"
#include "kernel/status.h"

#include <stddef.h>
#include <string.h>

#define SOCKET_TABLE_SIZE 64
#define SOCKET_FD_BASE    512
#define SOCKET_TYPE_STREAM 1
#define SOCKET_SOL_SOCKET 1
#define SOCKET_SO_REUSEADDR 2
#define SOCKET_SO_ERROR 4
#define SOCKET_ERR_ETIMEDOUT 110

#define SOCKET_POLL_IN   0x0001u
#define SOCKET_POLL_OUT  0x0004u
#define SOCKET_POLL_ERR  0x0008u
#define SOCKET_POLL_HUP  0x0010u

typedef struct {
    uint8_t used;
    uint8_t type;
    int32_t owner_pid;
    int32_t connection_id;
    uint16_t bound_port;
    uint16_t backlog;
    uint8_t reuse_address;
    uint8_t shutdown_read;
    uint8_t shutdown_write;
    int32_t last_error;
} kernel_socket_t;

static kernel_socket_t g_sockets[SOCKET_TABLE_SIZE];
static spinlock_t g_socket_lock;
static int g_socket_initialized;

static void socket_ensure_initialized(void)
{
    if (g_socket_initialized) return;
    spinlock_init(&g_socket_lock);
    memset(g_sockets, 0, sizeof(g_sockets));
    g_socket_initialized = 1;
}

static int32_t socket_index(int32_t fd)
{
    int32_t index = fd - SOCKET_FD_BASE;
    return index >= 0 && index < SOCKET_TABLE_SIZE ? index : -1;
}

static kernel_socket_t *socket_owned_locked(int32_t fd)
{
    int32_t index = socket_index(fd);
    int32_t pid = process_get_current_pid();
    if (index < 0 || pid < 0 || g_sockets[index].used == 0u ||
        g_sockets[index].owner_pid != pid)
        return NULL;
    return &g_sockets[index];
}

static uint16_t allocate_ephemeral_port(void)
{
    uint32_t start = (uint32_t)(timer_ticks() % 16384u);
    for (uint32_t attempt = 0; attempt < 16384u; ++attempt) {
        uint16_t candidate =
            (uint16_t)(49152u + ((start + attempt) % 16384u));
        if (!tcp_local_port_in_use(candidate)) return candidate;
    }
    return 0u;
}

int32_t syscall_socket_create(int32_t type)
{
    if (type != SOCKET_TYPE_STREAM) return (int32_t)OS_STATUS_NOT_SUPPORTED;
    int32_t pid = process_get_current_pid();
    if (pid < 0) return (int32_t)OS_STATUS_ACCESS_DENIED;

    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    for (int32_t index = 0; index < SOCKET_TABLE_SIZE; ++index) {
        if (g_sockets[index].used != 0u) continue;
        memset(&g_sockets[index], 0, sizeof(g_sockets[index]));
        g_sockets[index].used = 1u;
        g_sockets[index].type = (uint8_t)type;
        g_sockets[index].owner_pid = pid;
        g_sockets[index].connection_id = -1;
        spinlock_unlock(&g_socket_lock);
        return SOCKET_FD_BASE + index;
    }
    spinlock_unlock(&g_socket_lock);
    return (int32_t)OS_STATUS_LIMIT_REACHED;
}

int32_t syscall_socket_connect(int32_t fd, uint32_t ip, uint16_t port)
{
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *socket = socket_owned_locked(fd);
    if (socket == NULL || socket->connection_id >= 0 || ip == 0u || port == 0u) {
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    uint16_t local_port = socket->bound_port;
    spinlock_unlock(&g_socket_lock);

    if (local_port == 0u) local_port = allocate_ephemeral_port();
    if (local_port == 0u) return (int32_t)OS_STATUS_LIMIT_REACHED;
    int32_t connection_id = tcp_connect(ip, port, local_port);
    if (connection_id < 0) {
        spinlock_lock(&g_socket_lock);
        socket = socket_owned_locked(fd);
        if (socket != NULL) socket->last_error = 5;
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_IO_ERROR;
    }

    spinlock_lock(&g_socket_lock);
    socket = socket_owned_locked(fd);
    if (socket == NULL || socket->connection_id >= 0) {
        spinlock_unlock(&g_socket_lock);
        (void)tcp_close(connection_id);
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    socket->connection_id = connection_id;
    socket->bound_port = local_port;
    spinlock_unlock(&g_socket_lock);
    return 0;
}

int32_t syscall_socket_bind(int32_t fd, uint16_t port)
{
    if (port == 0u || tcp_local_port_in_use(port))
        return (int32_t)OS_STATUS_INVALID_ARG;
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *socket = socket_owned_locked(fd);
    if (socket == NULL || socket->connection_id >= 0 || socket->bound_port != 0u) {
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    for (int32_t index = 0; index < SOCKET_TABLE_SIZE; ++index) {
        kernel_socket_t *other = &g_sockets[index];
        if (other == socket || other->used == 0u ||
            other->bound_port != port) {
            continue;
        }
        if (socket->reuse_address == 0u || other->reuse_address == 0u) {
            spinlock_unlock(&g_socket_lock);
            return (int32_t)OS_STATUS_ACCESS_DENIED;
        }
    }
    socket->bound_port = port;
    spinlock_unlock(&g_socket_lock);
    return 0;
}

int32_t syscall_socket_listen(int32_t fd)
{
    return syscall_socket_listen_with_backlog(fd, 16);
}

int32_t syscall_socket_listen_with_backlog(int32_t fd, int32_t backlog)
{
    if (backlog <= 0) backlog = 1;
    if (backlog > (int32_t)TCP_MAX_CONNECTIONS) {
        backlog = (int32_t)TCP_MAX_CONNECTIONS;
    }
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *socket = socket_owned_locked(fd);
    if (socket == NULL || socket->connection_id >= 0 || socket->bound_port == 0u) {
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    uint16_t port = socket->bound_port;
    spinlock_unlock(&g_socket_lock);

    int32_t connection_id = tcp_listen(port);
    if (connection_id < 0) return (int32_t)OS_STATUS_IO_ERROR;
    if (tcp_set_listen_backlog(connection_id, (uint16_t)backlog) < 0) {
        (void)tcp_close(connection_id);
        return (int32_t)OS_STATUS_IO_ERROR;
    }

    spinlock_lock(&g_socket_lock);
    socket = socket_owned_locked(fd);
    if (socket == NULL || socket->connection_id >= 0) {
        spinlock_unlock(&g_socket_lock);
        (void)tcp_close(connection_id);
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    socket->connection_id = connection_id;
    socket->backlog = (uint16_t)backlog;
    spinlock_unlock(&g_socket_lock);
    return 0;
}

int32_t syscall_socket_accept(int32_t fd)
{
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *listener = socket_owned_locked(fd);
    if (listener == NULL || listener->connection_id < 0) {
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    int32_t listener_id = listener->connection_id;
    int32_t owner_pid = listener->owner_pid;
    spinlock_unlock(&g_socket_lock);

    int32_t connection_id = tcp_accept(listener_id);
    if (connection_id < 0) return -11;

    spinlock_lock(&g_socket_lock);
    for (int32_t index = 0; index < SOCKET_TABLE_SIZE; ++index) {
        if (g_sockets[index].used != 0u) continue;
        memset(&g_sockets[index], 0, sizeof(g_sockets[index]));
        g_sockets[index].used = 1u;
        g_sockets[index].type = SOCKET_TYPE_STREAM;
        g_sockets[index].owner_pid = owner_pid;
        g_sockets[index].connection_id = connection_id;
        tcp_connection_info_t info;
        if (tcp_get_connection_info(connection_id, &info) == 0) {
            g_sockets[index].bound_port = info.local_port;
        }
        spinlock_unlock(&g_socket_lock);
        return SOCKET_FD_BASE + index;
    }
    spinlock_unlock(&g_socket_lock);
    (void)tcp_close(connection_id);
    return (int32_t)OS_STATUS_LIMIT_REACHED;
}

static int32_t socket_connection_id(int32_t fd)
{
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *socket = socket_owned_locked(fd);
    int32_t connection_id = socket != NULL ? socket->connection_id : -1;
    spinlock_unlock(&g_socket_lock);
    return connection_id;
}

int32_t syscall_socket_send(int32_t fd, const void *data, uint16_t length)
{
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *socket = socket_owned_locked(fd);
    if (socket == NULL || socket->shutdown_write != 0u) {
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_ACCESS_DENIED;
    }
    spinlock_unlock(&g_socket_lock);
    int32_t connection_id = socket_connection_id(fd);
    if (connection_id < 0) return (int32_t)OS_STATUS_INVALID_ARG;
    int32_t result = tcp_send(connection_id, data, length);
    return result < 0 ? (int32_t)OS_STATUS_IO_ERROR : result;
}

int32_t syscall_socket_recv(int32_t fd, void *data, uint16_t length)
{
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *socket = socket_owned_locked(fd);
    if (socket == NULL || socket->shutdown_read != 0u) {
        spinlock_unlock(&g_socket_lock);
        return socket == NULL ?
            (int32_t)OS_STATUS_INVALID_ARG : 0;
    }
    spinlock_unlock(&g_socket_lock);
    int32_t connection_id = socket_connection_id(fd);
    if (connection_id < 0) return (int32_t)OS_STATUS_INVALID_ARG;
    int32_t result = tcp_recv(connection_id, data, length);
    return result < 0 ? (int32_t)OS_STATUS_IO_ERROR : result;
}

int32_t syscall_socket_close(int32_t fd)
{
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *socket = socket_owned_locked(fd);
    if (socket == NULL) {
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    int32_t connection_id = socket->connection_id;
    memset(socket, 0, sizeof(*socket));
    spinlock_unlock(&g_socket_lock);
    if (connection_id >= 0) (void)tcp_close(connection_id);
    return 0;
}

int32_t syscall_socket_get_info(int32_t fd, syscall_socket_info_t *info_out)
{
    if (info_out == NULL) return (int32_t)OS_STATUS_INVALID_ARG;
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *socket = socket_owned_locked(fd);
    if (socket == NULL) {
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    int32_t connection_id = socket->connection_id;
    memset(info_out, 0, sizeof(*info_out));
    info_out->local_port = socket->bound_port;
    info_out->error = socket->last_error;
    spinlock_unlock(&g_socket_lock);

    if (connection_id >= 0) {
        tcp_connection_info_t tcp_info;
        if (tcp_get_connection_info(connection_id, &tcp_info) == 0) {
            info_out->local_ip = tcp_info.local_ip;
            info_out->remote_ip = tcp_info.remote_ip;
            info_out->local_port = tcp_info.local_port;
            info_out->remote_port = tcp_info.remote_port;
            info_out->state = (uint32_t)tcp_info.state;
        }
    }
    return 0;
}

int32_t syscall_socket_set_option(int32_t fd, int32_t level,
                                  int32_t option, int32_t value)
{
    if (level != SOCKET_SOL_SOCKET || option != SOCKET_SO_REUSEADDR) {
        return (int32_t)OS_STATUS_NOT_SUPPORTED;
    }
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *socket = socket_owned_locked(fd);
    if (socket == NULL || socket->bound_port != 0u ||
        socket->connection_id >= 0) {
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    socket->reuse_address = value != 0 ? 1u : 0u;
    spinlock_unlock(&g_socket_lock);
    return 0;
}

int32_t syscall_socket_get_option(int32_t fd, int32_t level,
                                  int32_t option, int32_t *value_out)
{
    if (value_out == NULL || level != SOCKET_SOL_SOCKET) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *socket = socket_owned_locked(fd);
    if (socket == NULL) {
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    if (option == SOCKET_SO_REUSEADDR) {
        *value_out = socket->reuse_address != 0u ? 1 : 0;
        spinlock_unlock(&g_socket_lock);
        return 0;
    } else if (option == SOCKET_SO_ERROR) {
        int32_t error = socket->last_error;
        int32_t connection_id = socket->connection_id;
        socket->last_error = 0;
        spinlock_unlock(&g_socket_lock);

        if (error == 0 && connection_id >= 0) {
            tcp_connection_info_t info;
            if (tcp_get_connection_info(connection_id, &info) < 0 ||
                info.state == TCP_STATE_CLOSED) {
                error = SOCKET_ERR_ETIMEDOUT;
            }
        }
        *value_out = error;
        return 0;
    } else {
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_NOT_SUPPORTED;
    }
}

int32_t syscall_socket_shutdown(int32_t fd, int32_t how)
{
    if (how < 0 || how > 2) return (int32_t)OS_STATUS_INVALID_ARG;
    socket_ensure_initialized();
    spinlock_lock(&g_socket_lock);
    kernel_socket_t *socket = socket_owned_locked(fd);
    if (socket == NULL || socket->connection_id < 0) {
        spinlock_unlock(&g_socket_lock);
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    int32_t connection_id = socket->connection_id;
    if (how == 0 || how == 2) socket->shutdown_read = 1u;
    int close_write = 0;
    if ((how == 1 || how == 2) && socket->shutdown_write == 0u) {
        socket->shutdown_write = 1u;
        close_write = 1;
    }
    spinlock_unlock(&g_socket_lock);
    if (close_write && tcp_close(connection_id) < 0) {
        return (int32_t)OS_STATUS_IO_ERROR;
    }
    return 0;
}

int64_t syscall_socket_available(int32_t fd)
{
    int32_t connection_id = socket_connection_id(fd);
    if (connection_id < 0) return (int64_t)OS_STATUS_INVALID_ARG;
    tcp_connection_info_t info;
    if (tcp_get_connection_info(connection_id, &info) < 0) {
        return (int64_t)OS_STATUS_IO_ERROR;
    }
    return (int64_t)info.receive_available;
}

uint32_t syscall_socket_poll(int32_t fd, uint32_t events)
{
    int32_t connection_id = socket_connection_id(fd);
    if (connection_id < 0) return SOCKET_POLL_ERR;
    return tcp_poll(connection_id, events);
}

void syscall_socket_close_all_for_pid(int32_t pid)
{
    if (pid < 0) return;
    socket_ensure_initialized();
    for (;;) {
        int32_t connection_id = -1;
        int found = 0;
        spinlock_lock(&g_socket_lock);
        for (int32_t index = 0; index < SOCKET_TABLE_SIZE; ++index) {
            if (g_sockets[index].used == 0u ||
                g_sockets[index].owner_pid != pid)
                continue;
            connection_id = g_sockets[index].connection_id;
            memset(&g_sockets[index], 0, sizeof(g_sockets[index]));
            found = 1;
            break;
        }
        spinlock_unlock(&g_socket_lock);
        if (!found) break;
        if (connection_id >= 0) (void)tcp_close(connection_id);
    }
}
