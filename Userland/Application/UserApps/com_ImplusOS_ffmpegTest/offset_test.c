#include <stdio.h>
#include <stddef.h>
#include <libavutil/frame.h>
#include <libavcodec/avcodec.h>

int main() {
    printf("AVFrame.hw_frames_ctx = 0x%zx\n", offsetof(AVFrame, hw_frames_ctx));
    printf("AVFrame.opaque_ref = 0x%zx\n", offsetof(AVFrame, opaque_ref));
    printf("AVFrame.private_ref = 0x%zx\n", offsetof(AVFrame, private_ref));
    printf("AVFrame.qp_table_buf = 0x%zx\n", offsetof(AVFrame, qp_table_buf));
    printf("AVPacket.buf = 0x%zx\n", offsetof(AVPacket, buf));
    printf("AVPacket.opaque_ref = 0x%zx\n", offsetof(AVPacket, opaque_ref));
    return 0;
}
