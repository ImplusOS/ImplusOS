#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct gzFile_s {
    unsigned have;
    unsigned char *next;
    long long pos;
} gzFile_s;

typedef gzFile_s *gzFile;

typedef struct {
    gzFile_s exposed;
    FILE* fp;
} implus_gz_file_t;

gzFile gzopen(const char* path, const char* mode)
{
    implus_gz_file_t* file;
    FILE* fp;

    if (!path || !mode || mode[0] != 'r') {
        errno = EINVAL;
        return NULL;
    }

    fp = fopen(path, "r");
    if (!fp) {
        return NULL;
    }

    file = (implus_gz_file_t*)malloc(sizeof(*file));
    if (!file) {
        fclose(fp);
        errno = ENOMEM;
        return NULL;
    }

    file->exposed.have = 0;
    file->exposed.next = NULL;
    file->exposed.pos = 0;
    file->fp = fp;
    return &file->exposed;
}

char* gzgets(gzFile file, char* buf, int len)
{
    if (!file || !buf || len <= 0) {
        errno = EINVAL;
        return NULL;
    }
    return fgets(buf, len, ((implus_gz_file_t*)file)->fp);
}

int gzclose(gzFile file)
{
    int ret;

    if (!file) {
        errno = EINVAL;
        return -1;
    }

    ret = fclose(((implus_gz_file_t*)file)->fp);
    free(file);
    return ret;
}
