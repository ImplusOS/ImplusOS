#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static void dump_dir(const char* path)
{
    DIR* dir = opendir(path);
    struct dirent* ent;

    if (!dir) {
        printf("opendir(%s) failed: %s\n", path, strerror(errno));
        return;
    }

    printf("directory listing for %s:\n", path);
    while ((ent = readdir(dir)) != NULL) {
        printf("  [%s] %s\n", ent->d_type == DT_DIR ? "dir" : "file", ent->d_name);
    }
    closedir(dir);
}

static void copy_demo_file(void)
{
    const char* src_path = "/Userland/UserApps/org_ffmpeg_git_ffmpeg_git/Resource/demo_input.txt";
    const char* dst_path = "/ffmpeg_demo_output.txt";
    char buffer[128];
    struct stat st;
    int src;
    int dst;
    ssize_t nread;

    if (stat(src_path, &st) < 0) {
        printf("stat(%s) failed: %s\n", src_path, strerror(errno));
        return;
    }

    printf("input file size: %ld bytes\n", (long)st.st_size);

    src = open(src_path, O_RDONLY);
    if (src < 0) {
        printf("open(%s) failed: %s\n", src_path, strerror(errno));
        return;
    }

    dst = open(dst_path, O_CREAT | O_WRONLY | O_TRUNC);
    if (dst < 0) {
        printf("open(%s) failed: %s\n", dst_path, strerror(errno));
        close(src);
        return;
    }

    while ((nread = read(src, buffer, sizeof(buffer))) > 0) {
        if (write(dst, buffer, (size_t)nread) != nread) {
            printf("write failed: %s\n", strerror(errno));
            break;
        }
    }

    close(dst);
    close(src);
    printf("copied %s -> %s\n", src_path, dst_path);
}

static void show_time_and_memory_demo(void)
{
    struct timeval tv;
    struct timespec ts;
    void* page;

    gettimeofday(&tv, NULL);
    printf("uptime-based timeval: %ld.%06ld\n", (long)tv.tv_sec, (long)tv.tv_usec);

    ts.tv_sec = 0;
    ts.tv_nsec = 5 * 1000 * 1000;
    nanosleep(&ts, NULL);

    page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (page == MAP_FAILED) {
        printf("mmap failed: %s\n", strerror(errno));
        return;
    }

    strcpy((char*)page, "ImplusOS FFmpeg compatibility demo buffer");
    printf("mmap buffer says: %s\n", (char*)page);
    munmap(page, 4096);
}

void _start(void)
{
    printf("ImplusOS FFmpeg portability demo\n");
    printf("pid=%d ppid=%d\n", getpid(), getppid());

    dump_dir("/");
    copy_demo_file();
    show_time_and_memory_demo();

    printf("demo complete. This app exercises the new libc/POSIX compatibility layer.\n");
    _exit(0);
}
