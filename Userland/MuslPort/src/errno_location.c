#include <errno.h>

static int g_errno_value;

int *__errno_location(void)
{
    return &g_errno_value;
}

int *___errno_location(void)
{
    return __errno_location();
}
