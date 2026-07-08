#include <File.h>
#include <Jpeg.h>
#include <Process.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define JPEGTEST_PATH "/Userland/UserApps/com_ImplusOS_jpegTest/Resource/Test.jpg"
#define JPEGTEST_MAX_FILE_BYTES (16u * 1024u * 1024u)

static int read_file_to_memory(const char *path, uint8_t **out_data,
                               size_t *out_size)
{
    file_stat_t stat;
    if (!path || !out_data || !out_size) return -1;

    *out_data = NULL;
    *out_size = 0u;

    if (file_stat(path, &stat) < 0 || !stat.exists || stat.is_dir) {
        printf("jpegTest: file not found: %s\n", path);
        return -1;
    }

    if (stat.size == 0u || stat.size > JPEGTEST_MAX_FILE_BYTES) {
        printf("jpegTest: unsupported file size: %u\n", (unsigned)stat.size);
        return -1;
    }

    uint8_t *data = (uint8_t *)malloc((size_t)stat.size);
    if (!data) {
        printf("jpegTest: allocation failed\n");
        return -1;
    }

    int32_t fd = file_open(path, 0);
    if (fd < 0) {
        free(data);
        printf("jpegTest: unable to open file\n");
        return -1;
    }

    size_t total = 0u;
    while (total < (size_t)stat.size) {
        int64_t n = file_read(fd, data + total,
                              (uint64_t)((size_t)stat.size - total));
        if (n <= 0) {
            file_close(fd);
            free(data);
            printf("jpegTest: read failed\n");
            return -1;
        }
        total += (size_t)n;
    }

    file_close(fd);
    *out_data = data;
    *out_size = total;
    return 0;
}

static uint32_t checksum_image(const jpeg_image_t *image)
{
    uint32_t checksum = 2166136261u;
    size_t byte_count = (size_t)image->width * (size_t)image->height * 4u;
    size_t step = byte_count / 4096u;
    if (step == 0u) step = 1u;

    for (size_t i = 0u; i < byte_count; i += step) {
        checksum ^= image->rgba[i];
        checksum *= 16777619u;
    }
    return checksum;
}

int main(void)
{
    printf("jpegTest: loading %s\n", JPEGTEST_PATH);

    uint8_t *data = NULL;
    size_t size = 0u;
    if (read_file_to_memory(JPEGTEST_PATH, &data, &size) < 0) return 1;

    jpeg_image_t image = { 0u, 0u, NULL };
    int rc = jpeg_decode_rgba_from_memory(data, size, &image);
    free(data);

    if (rc < 0) {
        printf("jpegTest: decode failed\n");
        return 1;
    }

    printf("jpegTest: decoded %u x %u RGBA, checksum 0x%08x\n",
           (unsigned)image.width, (unsigned)image.height,
           (unsigned)checksum_image(&image));

    jpeg_free_image(&image);
    printf("jpegTest: passed\n");
    return 0;
}

void _start(void)
{
    int32_t status = (int32_t)main();
    process_exit(status);
    for (;;) {
        process_yield();
    }
}

