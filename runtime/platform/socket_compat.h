// Burnout Revenge. GPL-3.0-or-later (see COPYING).
#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
namespace platform::sockets
{
    inline void SetAddressLength(sockaddr_in& address)
    {
#ifdef __APPLE__
        address.sin_len = sizeof(address);
#endif
    }
    inline void SuppressSigpipe(int fd)
    {
#ifdef SO_NOSIGPIPE
        const int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    }
#ifdef MSG_NOSIGNAL
    inline constexpr int kSendFlags = MSG_NOSIGNAL;
#else
    inline constexpr int kSendFlags = 0;
#endif
}
