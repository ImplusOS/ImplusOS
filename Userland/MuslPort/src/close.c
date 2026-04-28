#include <unistd.h>
#include "syscall.h"

int close(int fd)
{
    return __syscall_ret(__syscall_cp(SYS_close, fd));
}
