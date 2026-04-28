 

#include "../include/posix_mman.h"
#include "../include/posix_fdtable.h"
#include "../include/posix_errno.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

 

extern void    *os_mmap(uint64_t length, uint64_t flags);
extern int64_t  file_read (int32_t fd, void *buffer, uint64_t len);
extern int64_t  file_seek (int32_t fd, int64_t offset, int32_t whence);

 

void *posix_mmap(void *addr, size_t length, int prot, int flags,
                 int fd, off_t offset)
{
    (void)addr;
    (void)prot;

    if (length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }

     
    void *ptr = os_mmap((uint64_t)length, 0);
    if (!ptr) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

     
    if (!(flags & MAP_ANONYMOUS) && fd >= 0) {
         
        file_seek((int32_t)fd, (int64_t)offset, 0  );
        int64_t r = file_read((int32_t)fd, ptr, (uint64_t)length);
        if (r < 0) {
             
            memset(ptr, 0, length);
        } else if ((size_t)r < length) {
             
            memset((char *)ptr + r, 0, length - (size_t)r);
        }
    } else {
         
        memset(ptr, 0, length);
    }

    os_errno = 0;
    return ptr;
}

 

int posix_munmap(void *addr, size_t length)
{
    (void)addr;
    (void)length;
     
    os_errno = 0;
    return 0;
}

 

int posix_mprotect(void *addr, size_t length, int prot)
{
    (void)addr;
    (void)length;
    (void)prot;
     
    os_errno = 0;
    return 0;
}
