#pragma once

typedef struct DIR DIR;

struct dirent {
    char d_name[260];
    unsigned char d_type;
};

struct DIR {
    int handle;
    struct dirent entry;
};

#define DT_UNKNOWN 0
#define DT_REG     8
#define DT_DIR     4

DIR* opendir(const char* path);
struct dirent* readdir(DIR* dirp);
int closedir(DIR* dirp);
int dirfd(DIR* dirp);
int scandir(const char* dirp, struct dirent*** namelist,
            int (*filter)(const struct dirent*),
            int (*compar)(const struct dirent**, const struct dirent**));
int alphasort(const struct dirent** a, const struct dirent** b);
