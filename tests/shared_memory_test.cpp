// Burnout Revenge. GPL-3.0-or-later (see COPYING).
// Charge >64 MiB through the same memfd factory as the runtime, with aliases.
#include <platform/shared_memory.h>
#include <cassert>
#include <fcntl.h>
#include <sys/stat.h>
int main()
{
    constexpr size_t guestBytes = size_t(1) << 32;
    auto* guest = static_cast<volatile unsigned char*>(platform::ReserveGuestAddressSpace(nullptr, guestBytes));
    assert(guest != MAP_FAILED);
    guest[4096] = 29;
    guest[guestBytes - 1] = 31;
    assert(guest[4096] == 29 && guest[guestBytes - 1] == 31);
    assert(munmap(const_cast<unsigned char*>(guest), guestBytes) == 0);

    constexpr size_t bytes = 80 * 1024 * 1024;
    const int fd = platform::CreatePhysicalMemoryFile(512 * 1024 * 1024);
    assert(fd >= 0 && (fcntl(fd, F_GETFD) & FD_CLOEXEC));
    struct stat info{};
    assert(fstat(fd, &info) == 0 && info.st_size == 512 * 1024 * 1024);
    auto* a = static_cast<volatile unsigned char*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    auto* b = static_cast<volatile unsigned char*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    assert(a != MAP_FAILED && b != MAP_FAILED);
    close(fd);
    for (size_t at = 0; at < bytes; at += 4096) a[at] = 123;
    for (size_t at = 0; at < bytes; at += 4096) assert(b[at] == 123);
    b[bytes - 1] = 74;
    assert(a[bytes - 1] == 74);
    assert(munmap(const_cast<unsigned char*>(a), bytes) == 0);
    assert(munmap(const_cast<unsigned char*>(b), bytes) == 0);
}
