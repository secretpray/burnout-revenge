// Burnout Revenge. GPL-3.0-or-later (see COPYING).
// A disconnected peer returns EPIPE, without changing global SIGPIPE handling.
#include <platform/socket_compat.h>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
int main()
{
    const pid_t child = fork();
    assert(child >= 0);
    if (!child)
    {
        signal(SIGPIPE, SIG_DFL);
        int pair[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        platform::sockets::SuppressSigpipe(pair[0]);
        close(pair[1]);
        assert(send(pair[0], "x", 1, platform::sockets::kSendFlags) == -1 && errno == EPIPE);
        close(pair[0]);
        sockaddr_in address{};
        platform::sockets::SetAddressLength(address);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
