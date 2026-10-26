// Burnout Revenge. GPL-3.0-or-later (see COPYING).
#pragma once
#include <csignal>
namespace platform
{
    inline bool IsWriteWatchFault(int signal, int code)
    {
#ifdef __linux__
        // mprotect guards cause SEGV_ACCERR. A truncated/exhausted backing
        // object raises SIGBUS; retrying it cannot repair the missing storage.
        return signal == SIGSEGV && code == SEGV_ACCERR;
#else
        // Darwin delivers both signals for protected mappings.
        return signal == SIGSEGV || signal == SIGBUS;
#endif
    }
}
