#pragma once

#include <sys/types.h>
#include <time.h>

struct stat {
    mode_t st_mode;
    off_t  st_size;
    uid_t  st_uid;
    gid_t  st_gid;
    time_t st_mtime;
    time_t st_atime;
    time_t st_ctime;
    nlink_t st_nlink;
};

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFLNK 0120000

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)

int stat(const char* path, struct stat* st);
int fstat(int fd, struct stat* st);
int mkdir(const char* path, mode_t mode);
int unlink(const char* path);
