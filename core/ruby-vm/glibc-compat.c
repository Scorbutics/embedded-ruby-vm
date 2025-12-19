/*
 * glibc compatibility shims for older glibc versions
 * Provides fallback implementations for newer glibc functions
 * that Ruby might use but aren't available in older glibc (< 2.25)
 * 
 * These functions are only compiled if they don't exist in the target glibc.
 * Use weak symbols so they don't conflict with existing implementations.
 */

#define _GNU_SOURCE
#include <features.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

// Always provide weak symbols - they will be used if the real functions aren't available at link time
// The linker will prefer the real glibc implementation if it exists

// Provide getentropy (added in glibc 2.25)
__attribute__((weak))
int getentropy(void *buffer, size_t length) {
    // Fallback to getrandom syscall
    #ifdef SYS_getrandom
    int ret = syscall(SYS_getrandom, buffer, length, 0);
    if (ret >= 0) {
        return 0;
    }
    #endif
    
    // Ultimate fallback: read from /dev/urandom
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        errno = EIO;
        return -1;
    }
    
    ssize_t result = read(fd, buffer, length);
    close(fd);
    
    if (result < 0 || (size_t)result != length) {
        errno = EIO;
        return -1;
    }
    
    return 0;
}

__attribute__((weak))
void explicit_bzero(void *s, size_t n) {
    // Prevent compiler from optimizing away the memset
    memset(s, 0, n);
    __asm__ __volatile__("" : : "r"(s) : "memory");
}

__attribute__((weak))
void __explicit_bzero_chk(void *s, size_t n, size_t buflen) {
    if (n > buflen) {
        __builtin_trap();  // Buffer overflow
    }
    explicit_bzero(s, n);
}

// Provide copy_file_range (added in glibc 2.27)
__attribute__((weak))
ssize_t copy_file_range(int fd_in, off_t *off_in, int fd_out, off_t *off_out,
                        size_t len, unsigned int flags) {
    #ifdef SYS_copy_file_range
    return syscall(SYS_copy_file_range, fd_in, off_in, fd_out, off_out, len, flags);
    #else
    errno = ENOSYS;
    return -1;
    #endif
}

// Provide statx (added in glibc 2.28)  
// Only define if statx isn't already declared in headers
#if !defined(__GLIBC__) || (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 28)

// Forward declare statx structure if not available
#ifndef STATX_TYPE
struct statx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __spare0[1];
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    struct {
        int64_t tv_sec;
        uint32_t tv_nsec;
        int32_t __reserved;
    } stx_atime, stx_btime, stx_ctime, stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t __spare2[14];
};

__attribute__((weak))
int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf) {
    #ifdef SYS_statx
    return syscall(SYS_statx, dirfd, pathname, flags, mask, statxbuf);
    #else
    // Fallback to fstatat
    struct stat st;
    int ret = fstatat(dirfd, pathname, &st, flags);
    if (ret < 0) {
        return ret;
    }
    
    // Convert stat to statx
    memset(statxbuf, 0, sizeof(*statxbuf));
    statxbuf->stx_mask = 0x7FF;  // Basic stats
    statxbuf->stx_blksize = st.st_blksize;
    statxbuf->stx_nlink = st.st_nlink;
    statxbuf->stx_uid = st.st_uid;
    statxbuf->stx_gid = st.st_gid;
    statxbuf->stx_mode = st.st_mode;
    statxbuf->stx_ino = st.st_ino;
    statxbuf->stx_size = st.st_size;
    statxbuf->stx_blocks = st.st_blocks;
    statxbuf->stx_atime.tv_sec = st.st_atime;
    statxbuf->stx_ctime.tv_sec = st.st_ctime;
    statxbuf->stx_mtime.tv_sec = st.st_mtime;
    
    return 0;
    #endif
}
#endif  // STATX_TYPE check

#endif  // glibc < 2.28
