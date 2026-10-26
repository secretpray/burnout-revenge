// Burnout Revenge. GPL-3.0-or-later (see COPYING).
// Real protection faults remain watchable; a missing backing store must escape.
#include <platform/fault_policy.h>
#include <cassert>
#include <initializer_list>
#include <csignal>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
static void Handler(int sig, siginfo_t* info, void*)
{
    _exit(platform::IsWriteWatchFault(sig, info->si_code) ? 42 : 43);
}
int main()
{
    assert(platform::IsWriteWatchFault(SIGSEGV, SEGV_ACCERR));
#ifdef __linux__
    assert(!platform::IsWriteWatchFault(SIGSEGV, SEGV_MAPERR));
    assert(!platform::IsWriteWatchFault(SIGBUS, BUS_ADRERR));
#endif
    for (bool backing : {false, true})
    {
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0)
        {
            struct sigaction action{};
            action.sa_sigaction = Handler;
            action.sa_flags = SA_SIGINFO;
            sigaction(SIGSEGV, &action, nullptr);
            sigaction(SIGBUS, &action, nullptr);
            if (backing)
            {
                char name[] = "/tmp/burnout-fault-XXXXXX";
                const int fd = mkstemp(name);
                assert(fd >= 0); unlink(name);
                assert(ftruncate(fd, 4096) == 0);
                auto* p = static_cast<volatile char*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
                assert(p != MAP_FAILED && ftruncate(fd, 0) == 0); close(fd);
                *p = 1;  // SIGBUS, never a watch fault
            }
            else
            {
                auto* p = static_cast<volatile char*>(mmap(nullptr, 4096, PROT_READ, MAP_ANON | MAP_PRIVATE, -1, 0));
                assert(p != MAP_FAILED); *p = 1;
            }
            _exit(99);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
        assert(WEXITSTATUS(status) == (backing ? 43 : 42));
    }
}
