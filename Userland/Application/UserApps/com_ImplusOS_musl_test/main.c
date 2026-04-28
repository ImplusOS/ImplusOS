#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

static size_t cstr_len(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

static size_t append_cstr(char *dst, size_t at, size_t cap, const char *src)
{
    size_t i = 0;
    while (src[i] != '\0' && at + 1 < cap) {
        dst[at++] = src[i++];
    }
    if (at < cap) {
        dst[at] = '\0';
    }
    return at;
}

static size_t append_u32(char *dst, size_t at, size_t cap, uint32_t value)
{
    char tmp[16];
    size_t n = 0;

    if (value == 0) {
        if (at + 1 < cap) {
            dst[at++] = '0';
            dst[at] = '\0';
        }
        return at;
    }

    while (value > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n > 0 && at + 1 < cap) {
        dst[at++] = tmp[--n];
    }
    if (at < cap) {
        dst[at] = '\0';
    }
    return at;
}

void _start(void)
{
    const char *out_path = "/Userland/musl_port_test.txt";
    const char *banner = "musl minimal port test on ImplusOS\n";
    char line[128];
    size_t pos = 0;

    int fd = open(out_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        _Exit((uint8_t)(100 + (errno & 0x1f)));
    }

    (void)write(fd, banner, cstr_len(banner));

    pos = append_cstr(line, pos, sizeof(line), "pid=");
    pos = append_u32(line, pos, sizeof(line), (uint32_t)getpid());
    pos = append_cstr(line, pos, sizeof(line), " ppid=");
    pos = append_u32(line, pos, sizeof(line), (uint32_t)getppid());
    pos = append_cstr(line, pos, sizeof(line), "\n");
    (void)write(fd, line, pos);

    (void)close(fd);

    fd = open(out_path, O_RDONLY, 0);
    if (fd >= 0) {
        (void)lseek(fd, 0, SEEK_SET);
        {
            char verify[32];
            ssize_t r = read(fd, verify, sizeof(verify));
            if (r >= 0) {
                (void)r;
            }
        }
        (void)close(fd);
    }
    _Exit(0);
}
