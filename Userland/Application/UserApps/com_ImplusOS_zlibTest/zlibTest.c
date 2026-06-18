#include <stdio.h>
#include <string.h>
#include <Zlib.h>
#include <Process.h>

int main(void) {
    printf("Starting zlib test...\n");
    const char *text = "Hello, ImplusOS zlib compression test!";
    size_t text_len = strlen(text);

    zlib_buffer_t compressed = {0};
    int rc = zlib_compress((const uint8_t*)text, text_len, &compressed);
    if (rc != 0) {
        printf("Compression failed: %d\n", rc);
        return 1;
    }
    printf("Compressed size: %zu\n", compressed.size);

    zlib_buffer_t decompressed = {0};
    rc = zlib_decompress(compressed.data, compressed.size, &decompressed);
    if (rc != 0) {
        printf("Decompression failed: %d\n", rc);
        zlib_free_buffer(&compressed);
        return 1;
    }
    printf("Decompressed text: %s\n", (char*)decompressed.data);

    if (strcmp(text, (char*)decompressed.data) == 0) {
        printf("Zlib test passed!\n");
    } else {
        printf("Zlib test failed: content mismatch!\n");
    }

    zlib_free_buffer(&compressed);
    zlib_free_buffer(&decompressed);

    return 0;
}

void _start(void) {
    int32_t status = (int32_t)main();
    process_exit(status);
    for (;;) {
        process_yield();
    }
}
