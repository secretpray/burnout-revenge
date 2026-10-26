// Burnout Revenge. GPL-3.0-or-later (see COPYING).
#pragma once
#ifdef __linux__
#include <cerrno>
#include <cstddef>
#include <sys/mman.h>
#include <unistd.h>
namespace platform
{
    // Anonymous shared storage for physical aliases. Unlike shm_open, memfd
    // does not consume a container's separately capped /dev/shm mount.
    inline int CreatePhysicalMemoryFile(size_t bytes)
    {
        const int fd = memfd_create("burnout-revenge-physical", MFD_CLOEXEC);
        if (fd < 0) return -1;
        if (ftruncate(fd, off_t(bytes)) == 0) return fd;
        const int error = errno;
        close(fd);
        errno = error;
        return -1;
    }
}
#endif
