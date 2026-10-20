// Burnout-specific compatibility while bringing up the separate build.
// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include <ui/guest_keyboard.h>
#include <SDL3/SDL_stdinc.h>
#include "overlapped.h"
#include "guest_wformat.h"
#include "function.h"
#include <cpu/guest_thread.h>
#include <kernel/xdm.h>
#include <user/online.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <poll.h>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <cerrno>

PPC_EXTERN_FUNC(__imp__XMsgStartIORequest);
uint32_t NtWaitForMultipleObjectsEx(uint32_t count, be<uint32_t>* handles, uint32_t waitType,
    uint32_t waitMode, uint32_t alertable, be<int64_t>* timeout);
uint32_t NtCreateEvent(be<uint32_t>* handle, XOBJECT_ATTRIBUTES* attributes, uint32_t eventType, uint32_t initialState);
uint32_t NtSetEvent(uint32_t handle, be<uint32_t>* previousState);
uint32_t NtWaitForSingleObjectEx(uint32_t handle, uint32_t waitMode, uint32_t alertable, be<int64_t>* timeout);
uint32_t NtClearEvent(uint32_t handle);
uint32_t NtClose(uint32_t handle);

// Guest FP traps are not delivered by this runtime. Keep the host's FP
// exceptions masked, as on the other guest-entry paths.
PPC_FUNC(__imp__KeEnableFpuExceptions)
{
    ctx.r3.u64 = 0;
}

namespace
{
    std::mutex s_terminateMutex;
    std::map<uint32_t, uint32_t> s_terminateRoutines;
}

// Registration is retained for the lifetime of this process. Closing the
// host window currently terminates the entire guest, as in the NW runtime;
// delivery of guest title-unload callbacks is not implemented yet.
PPC_FUNC(__imp__ExRegisterTitleTerminateNotification)
{
    const uint32_t registration = ctx.r3.u32;
    if (!registration)
        return;
    const uint32_t routine = PPC_LOAD_U32(registration);
    std::lock_guard lock(s_terminateMutex);
    if (ctx.r4.u32)
        s_terminateRoutines[routine] = PPC_LOAD_U32(registration + 4);
    else
        s_terminateRoutines.erase(routine);
}

PPC_FUNC(__imp__KeQueryBasePriorityThread)
{
    // This runtime's KeSetBasePriorityThread does not change host priority
    // on macOS. Report its default guest base-priority adjustment.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XAudioGetSpeakerConfig)
{
    // Xenia's default digital/analog stereo speaker configuration.
    if (ctx.r3.u32)
        PPC_STORE_U32(ctx.r3.u32, 0x00010001);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMsgStartIORequestEx)
{
    // The sixth argument is unused (as in Xenia); dispatch and overlapped
    // completion have the same contract as XMsgStartIORequest.
    __imp__XMsgStartIORequest(ctx, base);
}

namespace
{
    std::string KeyboardUtf8(const be<uint16_t>* text)
    {
        if (!text) return {};
        std::vector<uint16_t> units;
        for (size_t i = 0; i < 4096 && text[i] != 0; ++i)
            units.push_back(text[i]);
        units.push_back(0);
        char* converted = SDL_iconv_string("UTF-8", "UTF-16LE", reinterpret_cast<const char*>(units.data()), units.size() * 2);
        std::string result = converted ? converted : "";
        SDL_free(converted);
        return result;
    }

    uint32_t ShowKeyboard(uint32_t user, uint32_t flags, const be<uint16_t>* initial,
        const be<uint16_t>* title, const be<uint16_t>* description, be<uint16_t>* output,
        uint32_t capacity, XXOVERLAPPED* overlapped)
    {
        // The asynchronous ABI used by Burnout. Capacity includes the terminator.
        if (user >= 4 || !output || !capacity || capacity > 4096 || !overlapped)
            return 87;
        overlapped->Error = 997;
        overlapped->dwExtendedError = 0;
        overlapped->Length = 0;
        auto complete = [output, capacity, overlapped](bool accept, const std::string& text)
        {
            if (accept)
            {
                char* converted = SDL_iconv_string("UTF-16LE", "UTF-8", text.c_str(), text.size() + 1);
                if (!converted)
                {
                    CompleteOverlapped(overlapped, 0, 87);
                    return;
                }
                const auto* units = reinterpret_cast<const uint16_t*>(converted);
                size_t length = 0;
                while (length + 1 < capacity && units[length]) ++length;
                // Do not split a UTF-16 surrogate pair at the buffer boundary.
                if (length && units[length - 1] >= 0xD800 && units[length - 1] <= 0xDBFF) --length;
                for (size_t i = 0; i < length; ++i) output[i] = units[i];
                output[length] = 0;
                SDL_free(converted);
            }
            fprintf(stderr, "[burnout] keyboard %s\n", accept ? "accepted" : "cancelled");
            CompleteOverlapped(overlapped, 0, accept ? 0 : 1223);
        };
        fprintf(stderr, "[burnout] keyboard requested, flags=%08X capacity=%u\n", flags, capacity);
        // Explicit automation-only input; normal launches show an editable dialog.
        if (const char* text = std::getenv("BR_TEST_KEYBOARD_TEXT"))
            complete(true, text);
        else if (!ui::RequestGuestKeyboard(KeyboardUtf8(title), KeyboardUtf8(description), KeyboardUtf8(initial), capacity, complete))
            return 170; // ERROR_BUSY
        return 997; // ERROR_IO_PENDING
    }
}
GUEST_FUNCTION_HOOK(__imp__XamShowKeyboardUI, ShowKeyboard);

PPC_FUNC(__imp__vswprintf)
{
    auto read = [base](uint32_t address, bool narrow)
    {
        if (!address) return std::u16string(u"(null)");
        std::u16string text;
        for (size_t i = 0; i < 65536; ++i)
        {
            uint16_t value = narrow ? base[address + i] : uint16_t(PPC_LOAD_U16(address + i * 2));
            if (!value) break;
            text += char16_t(value);
        }
        return text;
    };
    uint32_t args = ctx.r5.u32;
    std::u16string formatted;
    bool ok = ctx.r3.u32 && ctx.r4.u32 && guestformat::Wide(read(ctx.r4.u32, false), [&]
    {
        uint64_t value = PPC_LOAD_U64(args); args += 8; return value;
    }, read, formatted);
    if (ctx.r3.u32)
    {
        if (ok)
            for (size_t i = 0; i < formatted.size(); ++i) PPC_STORE_U16(ctx.r3.u32 + i * 2, formatted[i]);
        PPC_STORE_U16(ctx.r3.u32 + (ok ? formatted.size() * 2 : 0), 0);
    }
    if (!ok) fprintf(stderr, "[burnout] vswprintf unsupported format at %08X\n", ctx.r4.u32);
    ctx.r3.s64 = ok ? int64_t(formatted.size()) : -1;
}

PPC_FUNC(__imp__NetDll_XNetStartup)
{
    // Offline the link status below reports no cable. Online
    // (BURNOUT_ONLINE, user/online.h) a console with a working connection.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetCleanup) { ctx.r3.u64 = 0; }
// Online: XNET_ETHERNET_LINK_ACTIVE | 100MBPS | FULL_DUPLEX.
PPC_FUNC(__imp__NetDll_XNetGetEthernetLinkStatus) { ctx.r3.u64 = online::Enabled() ? 0x01 | 0x02 | 0x08 : 0; }

PPC_FUNC(__imp__NetDll_XNetGetTitleXnAddr)
{
    if (online::Enabled())
    {
        // A console with a static address behind a gateway, DNS set and
        // online: EA's DirtySock reads less than that as a problem.
        if (ctx.r4.u32)
            online::FillXnAddr(base + ctx.r4.u32, online::LocalAddress());
        ctx.r3.u64 = 0x02 | 0x04 | 0x20 | 0x40 | 0x80;  // ETHERNET STATIC GATEWAY DNS ONLINE
        return;
    }
    if (ctx.r4.u32)
        memset(base + ctx.r4.u32, 0, 36);  // XNADDR
    ctx.r3.u64 = 1;  // XNET_GET_XNADDR_NONE (offline, not pending).
}

PPC_FUNC(__imp__NetDll_WSAStartup)
{
    const uint32_t address = ctx.r5.u32;
    if (address)
    {
        // Guest WSADATA layout. Keep the game's vendor pointer intact.
        PPC_STORE_U16(address, ctx.r4.u16);
        PPC_STORE_U16(address + 2, 0x0202);
        base[address + 4] = 0;
        base[address + 261] = 0;
        PPC_STORE_U16(address + 390, 100);
        PPC_STORE_U16(address + 392, 1024);
    }
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_WSACleanup) { ctx.r3.u64 = 0; }

namespace
{
    thread_local uint32_t s_socketError = 0;
    struct OfflineSocket : KernelObject
    {
        int fd;
        explicit OfflineSocket(int fd) : fd(fd) {}
        ~OfflineSocket() override { close(fd); }
    };

    uint32_t SocketFailure(uint32_t error)
    {
        s_socketError = error;
        GuestThread::SetLastError(error);
        return 0xFFFFFFFF;
    }
}

PPC_FUNC(__imp__NetDll_WSAGetLastError) { ctx.r3.u64 = s_socketError; }

PPC_FUNC(__imp__NetDll_socket)
{
    // Burnout creates UDP sockets during offline startup. No external
    // network connection is made; bind below restricts them to loopback.
    if (ctx.r4.u32 != 2 || (ctx.r5.u32 != 1 && ctx.r5.u32 != 2))
    {
        ctx.r3.u64 = SocketFailure(10047);  // WSAEAFNOSUPPORT
        return;
    }
    const int type = ctx.r5.u32 == 2 ? SOCK_DGRAM : SOCK_STREAM;
    int fd = socket(AF_INET, type, 0);
    if (fd < 0)
    {
        ctx.r3.u64 = SocketFailure(10055);  // WSAENOBUFS
        return;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    ctx.r3.u64 = GetKernelHandle(CreateKernelObject<OfflineSocket>(fd));
}

PPC_FUNC(__imp__NetDll_ioctlsocket)
{
    auto* socket = dynamic_cast<OfflineSocket*>(GetKernelObject(ctx.r4.u32));
    if (!socket || !ctx.r6.u32)
        ctx.r3.u64 = SocketFailure(10038);  // WSAENOTSOCK
    else if (ctx.r5.u32 != 0x8004667E)
        ctx.r3.u64 = SocketFailure(10022);  // Only FIONBIO is supported.
    else
    {
        int flags = fcntl(socket->fd, F_GETFL);
        ctx.r3.u64 = fcntl(socket->fd, F_SETFL,
            PPC_LOAD_U32(ctx.r6.u32) ? flags | O_NONBLOCK : flags & ~O_NONBLOCK) < 0
            ? SocketFailure(10022) : 0;
    }
}

namespace { uint16_t HostPort(uint16_t guestPortNetwork); }

PPC_FUNC(__imp__NetDll_bind)
{
    auto* socket = dynamic_cast<OfflineSocket*>(GetKernelObject(ctx.r4.u32));
    const uint32_t addr = ctx.r5.u32;
    if (!socket || !addr || ctx.r6.u32 < 16 || PPC_LOAD_U16(addr) != 2)
    {
        ctx.r3.u64 = SocketFailure(10022);
        return;
    }
    sockaddr_in local{};
    local.sin_len = sizeof(local);
    local.sin_family = AF_INET;
    memcpy(&local.sin_port, base + addr + 2, 2);
    local.sin_port = HostPort(local.sin_port);
    // Offline every socket is on loopback. Online where the game asks, and
    // "any address" is this console's own when one is set, so two copies on
    // one machine can both have the game's fixed ports.
    if (online::Enabled())
    {
        memcpy(&local.sin_addr.s_addr, base + addr + 4, 4);
        if (local.sin_addr.s_addr == 0)
            local.sin_addr.s_addr = online::BindAddress();
        const int on = 1;
        setsockopt(socket->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    }
    else
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool ok = bind(socket->fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0;
    if (online::Enabled())
    {
        const uint8_t* ip = reinterpret_cast<const uint8_t*>(&local.sin_addr.s_addr);
        fprintf(stderr, "[net] bind %08X to %u.%u.%u.%u:%u%s\n", ctx.r4.u32, ip[0], ip[1], ip[2], ip[3], ntohs(local.sin_port),
            ok ? "" : " failed");
    }
    ctx.r3.u64 = ok ? 0 : SocketFailure(errno == EADDRINUSE ? 10048 : 10022);
}

PPC_FUNC(__imp__NetDll_WSAWaitForMultipleEvents)
{
    const uint32_t count = ctx.r3.u32, array = ctx.r4.u32;
    if (count == 0 || count > 64 || !array)
    {
        ctx.r3.u64 = SocketFailure(87);
        return;
    }
    be<int64_t> timeout = -int64_t(ctx.r6.u32) * 10000;
    const uint32_t status = NtWaitForMultipleObjectsEx(count,
        reinterpret_cast<be<uint32_t>*>(base + array), ctx.r5.u32 ? 0 : 1,
        1, ctx.r7.u32, ctx.r6.u32 == 0xFFFFFFFF ? nullptr : &timeout);
    ctx.r3.u64 = (status & 0x80000000) ? SocketFailure(10022) : status;
}

namespace { void DropPendingReceives(uint32_t handle); }

PPC_FUNC(__imp__NetDll_closesocket)
{
    const uint32_t handle = ctx.r4.u32;
    if (handle == 0xFFFFFFFF || !IsKernelObject(handle))
    {
        ctx.r3.u64 = SocketFailure(10038);
        return;
    }
    auto* socket = dynamic_cast<OfflineSocket*>(GetKernelObject(handle));
    if (!socket)
        ctx.r3.u64 = SocketFailure(10038);
    else
    {
        DropPendingReceives(handle);
        ReleaseKernelObject(socket);
        ctx.r3.u64 = 0;
    }
}

// ---- The rest of the title's network calls, offline ----------------------
//
// Choosing Xbox Live in the main menu starts EA's network layer: its thread
// polls the sockets above with select, and it looks up EA's servers. Offline,
// as a console without a connection: sockets stay on loopback (what is sent
// elsewhere is dropped, as a datagram may be), name and QoS lookups find
// nothing, connections fail. Behaviour after Xenia's xam_net.cc; code ours.

namespace
{
    OfflineSocket* SocketFromHandle(uint32_t handle)
    {
        if (handle == 0xFFFFFFFF || !IsKernelObject(handle))
            return nullptr;
        return dynamic_cast<OfflineSocket*>(GetKernelObject(handle));
    }

    uint32_t ErrnoToWsa(int error)
    {
        switch (error)
        {
        case EWOULDBLOCK: return 10035;   // WSAEWOULDBLOCK
        case EMSGSIZE: return 10040;      // WSAEMSGSIZE
        case ENOTCONN: return 10057;      // WSAENOTCONN
        case ECONNRESET: return 10054;    // WSAECONNRESET
        case ECONNREFUSED: return 10061;  // WSAECONNREFUSED
        case EINTR: return 10004;         // WSAEINTR
        case EINPROGRESS: return 10035;   // a non-blocking connect under way: WSAEWOULDBLOCK, as Winsock says
        case EALREADY: return 10037;      // WSAEALREADY
        case EISCONN: return 10056;       // WSAEISCONN
        case ETIMEDOUT: return 10060;     // WSAETIMEDOUT
        case ENETUNREACH: return 10051;   // WSAENETUNREACH
        case EHOSTUNREACH: return 10065;  // WSAEHOSTUNREACH
        case EADDRINUSE: return 10048;    // WSAEADDRINUSE
        case EPIPE: return 10058;         // WSAESHUTDOWN
        default: return 10022;            // WSAEINVAL
        }
    }

    // Two copies on one Mac (BURNOUT_ONLINE_ADDRESS, 127.0.0.2 and .3): macOS
    // lets a program without root bind a port below 1024 to "any address"
    // only, not to one address (EACCES), and the game's peer-to-peer
    // handshake uses port 1000. There, ports below 1024 are 30000 higher on
    // the Mac (1000 -> 31000) - bound, sent to and connected to - and turned
    // back in every address the game is told. Between two Macs (no address
    // set) nothing is moved.
    bool PortsMoved() { return online::Enabled() && online::BindAddress() != 0; }

    uint16_t HostPort(uint16_t guestPortNetwork)
    {
        const uint16_t port = ntohs(guestPortNetwork);
        return PortsMoved() && port > 0 && port < 1024 ? htons(uint16_t(port + 30000)) : guestPortNetwork;
    }

    uint16_t GuestPort(uint16_t hostPortNetwork)
    {
        const uint16_t port = ntohs(hostPortNetwork);
        return PortsMoved() && port >= 30001 && port < 31024 ? htons(uint16_t(port - 30000)) : hostPortNetwork;
    }

    // Guest sockaddr_in: family (big-endian 16 bits), port and address in
    // network order.
    void StoreGuestAddress(uint8_t* base, uint32_t address, uint32_t lengthAddress, const sockaddr_in& from)
    {
        if (address && (!lengthAddress || PPC_LOAD_U32(lengthAddress) >= 16))
        {
            memset(base + address, 0, 16);
            PPC_STORE_U16(address, 2);
            const uint16_t port = GuestPort(from.sin_port);
            memcpy(base + address + 2, &port, 2);
            memcpy(base + address + 4, &from.sin_addr.s_addr, 4);
        }
        if (lengthAddress)
            PPC_STORE_U32(lengthAddress, 16);
    }

    // Guest fd_set: a count, then up to 64 socket handles (big-endian).
    struct GuestSet
    {
        uint32_t address = 0;
        uint32_t count = 0;
        uint32_t handles[64]{};
        int fds[64]{};
    };

    bool LoadSet(uint8_t* base, uint32_t address, GuestSet& set)
    {
        set.address = address;
        if (!address)
            return true;
        set.count = std::min<uint32_t>(PPC_LOAD_U32(address), 64);
        for (uint32_t i = 0; i < set.count; ++i)
        {
            set.handles[i] = PPC_LOAD_U32(address + 4 + i * 4);
            auto* socket = SocketFromHandle(set.handles[i]);
            if (!socket)
                return false;
            set.fds[i] = socket->fd;
        }
        return true;
    }

    uint32_t s_dnsLookups = 0;

    // BURNOUT_LOG_NET=1: each network call below but select (counted).
    bool LogNet()
    {
        static const bool log = std::getenv("BURNOUT_LOG_NET") != nullptr;
        return log;
    }
}

#define NET_LOG(...) do { if (LogNet()) fprintf(stderr, "[net] " __VA_ARGS__); } while (0)

PPC_FUNC(__imp__NetDll_select)
{
    GuestSet sets[3];
    if (!LoadSet(base, ctx.r5.u32, sets[0]) || !LoadSet(base, ctx.r6.u32, sets[1]) || !LoadSet(base, ctx.r7.u32, sets[2]))
    {
        ctx.r3.u64 = SocketFailure(10038);  // WSAENOTSOCK
        return;
    }
    static constexpr short kEvents[3] = { POLLIN, POLLOUT, POLLPRI };
    std::vector<pollfd> fds;
    for (int k = 0; k < 3; ++k)
        for (uint32_t i = 0; i < sets[k].count; ++i)
            fds.push_back({ sets[k].fds[i], kEvents[k], 0 });
    int timeoutMs = -1;
    if (const uint32_t timeout = ctx.r8.u32)
    {
        const int64_t seconds = int32_t(PPC_LOAD_U32(timeout)), micros = int32_t(PPC_LOAD_U32(timeout + 4));
        timeoutMs = int(std::clamp<int64_t>(seconds * 1000 + (micros + 999) / 1000, 0, INT32_MAX));
    }
    const int result = poll(fds.data(), nfds_t(fds.size()), timeoutMs);
    if (LogNet())
    {
        static std::atomic<uint32_t> calls{ 0 };
        const uint32_t n = ++calls;
        if ((n & (n - 1)) == 0)
            NET_LOG("select #%u: %zu sockets, timeout %d ms -> %d\n", n, fds.size(), timeoutMs, result);
    }
    if (result < 0)
    {
        ctx.r3.u64 = SocketFailure(ErrnoToWsa(errno));
        return;
    }
    // Each set keeps its ready sockets; the result is their number.
    uint32_t ready = 0;
    size_t next = 0;
    for (int k = 0; k < 3; ++k)
    {
        if (!sets[k].address)
            continue;
        uint32_t kept = 0;
        for (uint32_t i = 0; i < sets[k].count; ++i, ++next)
        {
            const short revents = fds[next].revents;
            const bool isReady = k == 0 ? (revents & (POLLIN | POLLHUP | POLLERR)) != 0
                : k == 1 ? (revents & (POLLOUT | POLLERR)) != 0 : (revents & POLLPRI) != 0;
            if (isReady)
                PPC_STORE_U32(sets[k].address + 4 + kept++ * 4, sets[k].handles[i]);
        }
        PPC_STORE_U32(sets[k].address, kept);
        ready += kept;
    }
    ctx.r3.u64 = ready;
}

static void Receive(PPCContext& ctx, uint8_t* base, uint32_t from, uint32_t fromLength)
{
    auto* socket = SocketFromHandle(ctx.r4.u32);
    if (!socket)
    {
        ctx.r3.u64 = SocketFailure(10038);
        return;
    }
    sockaddr_in source{};
    socklen_t sourceLength = sizeof(source);
    const ssize_t n = recvfrom(socket->fd, base + ctx.r5.u32, ctx.r6.u32, 0, reinterpret_cast<sockaddr*>(&source), &sourceLength);
    if (n < 0)
    {
        ctx.r3.u64 = SocketFailure(ErrnoToWsa(errno));
        return;
    }
    if (from || fromLength)
        StoreGuestAddress(base, from, fromLength, source);
    NET_LOG("recv %08X: %zd bytes\n", ctx.r4.u32, n);
    ctx.r3.u64 = uint64_t(n);
}

PPC_FUNC(__imp__NetDll_recv) { Receive(ctx, base, 0, 0); }
PPC_FUNC(__imp__NetDll_recvfrom) { Receive(ctx, base, ctx.r8.u32, ctx.r9.u32); }

static void Send(PPCContext& ctx, uint8_t* base, uint32_t to, uint32_t toLength)
{
    auto* socket = SocketFromHandle(ctx.r4.u32);
    if (!socket)
    {
        ctx.r3.u64 = SocketFailure(10038);
        return;
    }
    ssize_t n;
    if (to && toLength >= 16)
    {
        uint8_t address[4];
        memcpy(address, base + to + 4, 4);
        NET_LOG("sendto %08X: %u bytes to %u.%u.%u.%u:%u\n", ctx.r4.u32, ctx.r6.u32, address[0], address[1], address[2], address[3],
            uint32_t(base[to + 2]) << 8 | base[to + 3]);
        if (online::Enabled())
        {
            // Online: where the game sends it.
            sockaddr_in target{};
            target.sin_len = sizeof(target);
            target.sin_family = AF_INET;
            memcpy(&target.sin_port, base + to + 2, 2);
            target.sin_port = HostPort(target.sin_port);
            memcpy(&target.sin_addr.s_addr, address, 4);
            n = sendto(socket->fd, base + ctx.r5.u32, ctx.r6.u32, 0, reinterpret_cast<sockaddr*>(&target), sizeof(target));
            ctx.r3.u64 = n < 0 ? SocketFailure(ErrnoToWsa(errno)) : uint64_t(n);
            return;
        }
        if (address[0] != 127 && (address[0] | address[1] | address[2] | address[3]) != 0)
        {
            ctx.r3.u64 = ctx.r6.u32;  // elsewhere: dropped on the way
            return;
        }
        sockaddr_in target{};
        target.sin_len = sizeof(target);
        target.sin_family = AF_INET;
        memcpy(&target.sin_port, base + to + 2, 2);
        target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        n = sendto(socket->fd, base + ctx.r5.u32, ctx.r6.u32, 0, reinterpret_cast<sockaddr*>(&target), sizeof(target));
    }
    else
        n = send(socket->fd, base + ctx.r5.u32, ctx.r6.u32, 0);
    ctx.r3.u64 = n < 0 ? SocketFailure(ErrnoToWsa(errno)) : uint64_t(n);
}

PPC_FUNC(__imp__NetDll_send) { Send(ctx, base, 0, 0); }
PPC_FUNC(__imp__NetDll_sendto) { Send(ctx, base, ctx.r8.u32, ctx.r9.u32); }

PPC_FUNC(__imp__NetDll_getsockname)
{
    auto* socket = SocketFromHandle(ctx.r4.u32);
    sockaddr_in local{};
    socklen_t length = sizeof(local);
    if (!socket)
        ctx.r3.u64 = SocketFailure(10038);
    else if (getsockname(socket->fd, reinterpret_cast<sockaddr*>(&local), &length) < 0)
        ctx.r3.u64 = SocketFailure(ErrnoToWsa(errno));
    else
    {
        StoreGuestAddress(base, ctx.r5.u32, ctx.r6.u32, local);
        ctx.r3.u64 = 0;
    }
}

// Offline no connection is ever made. Online (BURNOUT_ONLINE) the
// connection is real: DirtySock connects to the lobby server non-blocking and
// learns the outcome from select (writable) and getpeername.
PPC_FUNC(__imp__NetDll_connect)
{
    auto* socket = SocketFromHandle(ctx.r4.u32);
    const uint32_t to = ctx.r5.u32;
    if (!online::Enabled() || !socket)
    {
        NET_LOG("connect %08X: unreachable\n", ctx.r4.u32);
        ctx.r3.u64 = SocketFailure(socket ? 10051 : 10038);  // WSAENETUNREACH
        return;
    }
    if (!to || ctx.r6.u32 < 16 || PPC_LOAD_U16(to) != 2)
    {
        ctx.r3.u64 = SocketFailure(10047);  // WSAEAFNOSUPPORT
        return;
    }
    sockaddr_in target{};
    target.sin_len = sizeof(target);
    target.sin_family = AF_INET;
    memcpy(&target.sin_port, base + to + 2, 2);
    target.sin_port = HostPort(target.sin_port);
    memcpy(&target.sin_addr.s_addr, base + to + 4, 4);
    const int result = connect(socket->fd, reinterpret_cast<sockaddr*>(&target), sizeof(target));
    const int error = result < 0 ? errno : 0;
    const uint8_t* ip = reinterpret_cast<const uint8_t*>(&target.sin_addr.s_addr);
    fprintf(stderr, "[net] connect %08X to %u.%u.%u.%u:%u -> %s\n", ctx.r4.u32, ip[0], ip[1], ip[2], ip[3], ntohs(target.sin_port),
        result == 0 ? "connected" : error == EINPROGRESS ? "under way" : strerror(error));
    ctx.r3.u64 = result == 0 ? 0 : SocketFailure(ErrnoToWsa(error));
}

PPC_FUNC(__imp__NetDll_getpeername)
{
    auto* socket = SocketFromHandle(ctx.r4.u32);
    if (!socket || !online::Enabled())
    {
        ctx.r3.u64 = SocketFailure(socket ? 10057 : 10038);  // WSAENOTCONN
        return;
    }
    sockaddr_in peer{};
    socklen_t length = sizeof(peer);
    if (getpeername(socket->fd, reinterpret_cast<sockaddr*>(&peer), &length) < 0)
        ctx.r3.u64 = SocketFailure(ErrnoToWsa(errno));
    else
    {
        StoreGuestAddress(base, ctx.r5.u32, ctx.r6.u32, peer);
        ctx.r3.u64 = 0;
    }
}

PPC_FUNC(__imp__NetDll_accept)
{
    auto* socket = SocketFromHandle(ctx.r4.u32);
    if (!socket || !online::Enabled())
    {
        ctx.r3.u64 = SocketFailure(socket ? 10035 : 10038);  // WSAEWOULDBLOCK
        return;
    }
    sockaddr_in peer{};
    socklen_t length = sizeof(peer);
    const int fd = accept(socket->fd, reinterpret_cast<sockaddr*>(&peer), &length);
    if (fd < 0)
    {
        ctx.r3.u64 = SocketFailure(ErrnoToWsa(errno));
        return;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    StoreGuestAddress(base, ctx.r5.u32, ctx.r6.u32, peer);
    ctx.r3.u64 = GetKernelHandle(CreateKernelObject<OfflineSocket>(fd));
}

PPC_FUNC(__imp__NetDll_listen)
{
    auto* socket = SocketFromHandle(ctx.r4.u32);
    if (!socket)
        ctx.r3.u64 = SocketFailure(10038);
    else if (!online::Enabled())
        ctx.r3.u64 = 0;
    else
        ctx.r3.u64 = listen(socket->fd, std::max(1, int32_t(ctx.r5.u32))) < 0 ? SocketFailure(ErrnoToWsa(errno)) : 0;
}

PPC_FUNC(__imp__NetDll_shutdown)
{
    auto* socket = SocketFromHandle(ctx.r4.u32);
    if (!socket)
    {
        ctx.r3.u64 = SocketFailure(10038);
        return;
    }
    if (online::Enabled())
        ::shutdown(socket->fd, std::clamp(int32_t(ctx.r5.u32), 0, 2));  // SD_RECEIVE/SEND/BOTH are SHUT_RD/WR/RDWR
    ctx.r3.u64 = 0;
}

// Offline the options (broadcast, buffer sizes, non-blocking VDP) change
// nothing on loopback. Online the ones that matter are set: SOL_SOCKET
// (0xFFFF) SO_REUSEADDR 4, SO_BROADCAST 0x20, SO_SNDBUF 0x1001, SO_RCVBUF
// 0x1002; IPPROTO_TCP TCP_NODELAY 1. Others (the Xbox's own) are accepted.
PPC_FUNC(__imp__NetDll_setsockopt)
{
    auto* socket = SocketFromHandle(ctx.r4.u32);
    if (!socket)
    {
        ctx.r3.u64 = SocketFailure(10038);
        return;
    }
    ctx.r3.u64 = 0;
    const uint32_t level = ctx.r5.u32, name = ctx.r6.u32, value = ctx.r7.u32;
    if (!online::Enabled() || !value || ctx.r8.u32 < 4)
        return;
    const int v = int(PPC_LOAD_U32(value));
    int hostLevel = 0, hostName = 0;
    if (level == 0xFFFF && name == 4)
        hostLevel = SOL_SOCKET, hostName = SO_REUSEADDR;
    else if (level == 0xFFFF && name == 0x20)
        hostLevel = SOL_SOCKET, hostName = SO_BROADCAST;
    else if (level == 0xFFFF && name == 0x1001)
        hostLevel = SOL_SOCKET, hostName = SO_SNDBUF;
    else if (level == 0xFFFF && name == 0x1002)
        hostLevel = SOL_SOCKET, hostName = SO_RCVBUF;
    else if (level == 6 && name == 1)
        hostLevel = IPPROTO_TCP, hostName = TCP_NODELAY;
    if (hostName)
        setsockopt(socket->fd, hostLevel, hostName, &v, sizeof(v));
}

// Overlapped receives. Offline nothing ever arrives (as Xenia: an error, so
// the title doesn't wait for it). Online (BURNOUT_ONLINE) they are real: the
// game's peer link (CommUDP, port 8192) waits on them with WSA events. What
// is there already is taken at once; otherwise the receive is left pending,
// a thread of ours waits for the socket (poll), takes the datagram into the
// game's buffers and completes the WSAOVERLAPPED (Internal: status,
// InternalHigh: bytes) and sets its event.
//
// Guest WSABUF { len, buf }; WSAOVERLAPPED { Internal, InternalHigh, Offset,
// OffsetHigh, hEvent }.
namespace
{
    constexpr uint32_t kStatusPending = 0x103;

    // The game's WSABUF array is its own stack, gone once WSARecvFrom
    // returns (Winsock takes the buffers in the call): what it says is kept
    // here. Read later, it was another function's stack, and a datagram went
    // to wherever that pointed - DirtySock's socket list, which its thread
    // then walked into address 0x100 (both two-copy runs crashed so).
    struct GuestBuffers
    {
        uint32_t count = 0;
        uint32_t length[4]{}, address[4]{};
    };

    struct PendingReceive
    {
        uint32_t handle;
        GuestBuffers buffers;
        uint32_t from, fromLength, overlapped;
        int fd;
    };

    std::mutex s_receiveMutex;
    std::vector<PendingReceive> s_pendingReceives;
    int s_receiveWake[2] = { -1, -1 };

    GuestBuffers LoadBuffers(uint8_t* base, uint32_t buffers, uint32_t count)
    {
        GuestBuffers b;
        b.count = buffers ? std::min(count, 4u) : 0;
        for (uint32_t i = 0; i < b.count; i++)
        {
            b.length[i] = PPC_LOAD_U32(buffers + i * 8);
            b.address[i] = PPC_LOAD_U32(buffers + i * 8 + 4);
        }
        return b;
    }

    // Into the game's buffers; the bytes taken, or -1 with errno.
    ssize_t ReceiveInto(uint8_t* base, int fd, const GuestBuffers& buffers, uint32_t from, uint32_t fromLength, int flags)
    {
        uint8_t data[2048];
        sockaddr_in source{};
        socklen_t sourceLength = sizeof(source);
        const ssize_t n = recvfrom(fd, data, sizeof(data), flags, reinterpret_cast<sockaddr*>(&source), &sourceLength);
        if (n < 0)
            return n;
        size_t done = 0;
        for (uint32_t i = 0; i < buffers.count && done < size_t(n); i++)
        {
            const size_t part = std::min<size_t>(buffers.length[i], size_t(n) - done);
            if (buffers.address[i])
                memcpy(base + buffers.address[i], data + done, part);
            done += part;
        }
        if (from || fromLength)
            StoreGuestAddress(base, from, fromLength, source);
        return ssize_t(done);
    }

    void CompleteReceive(uint8_t* base, uint32_t overlapped, uint32_t status, uint32_t bytes)
    {
        PPC_STORE_U32(overlapped + 4, bytes);
        PPC_STORE_U32(overlapped, status);
        if (const uint32_t event = PPC_LOAD_U32(overlapped + 16))
            NtSetEvent(event, nullptr);
    }

    void ReceiveThread(uint8_t* base)
    {
        for (;;)
        {
            std::vector<pollfd> fds{ { s_receiveWake[0], POLLIN, 0 } };
            {
                std::lock_guard lock(s_receiveMutex);
                for (const auto& p : s_pendingReceives)
                    fds.push_back({ p.fd, POLLIN, 0 });
            }
            if (poll(fds.data(), nfds_t(fds.size()), 500) <= 0)
                continue;
            if (fds[0].revents & POLLIN)
            {
                char drain[64];
                while (read(s_receiveWake[0], drain, sizeof(drain)) > 0)
                {
                }
            }
            std::lock_guard lock(s_receiveMutex);
            for (size_t i = 1; i < fds.size(); i++)
            {
                if (!(fds[i].revents & (POLLIN | POLLERR | POLLHUP)))
                    continue;
                auto it = std::find_if(s_pendingReceives.begin(), s_pendingReceives.end(), [&](const PendingReceive& p) { return p.fd == fds[i].fd; });
                if (it == s_pendingReceives.end())
                    continue;
                const ssize_t n = ReceiveInto(base, it->fd, it->buffers, it->from, it->fromLength, MSG_DONTWAIT);
                if (n < 0 && errno == EWOULDBLOCK)
                    continue;
                CompleteReceive(base, it->overlapped, n < 0 ? 0xC000020C /* STATUS_CONNECTION_DISCONNECTED */ : 0, n < 0 ? 0 : uint32_t(n));
                s_pendingReceives.erase(it);
            }
        }
    }

    void Wake()
    {
        if (s_receiveWake[1] >= 0)
            (void)write(s_receiveWake[1], "w", 1);
    }

    // A pending receive on a socket being closed is dropped (Winsock aborts it).
    void DropPendingReceives(uint32_t handle)
    {
        std::lock_guard lock(s_receiveMutex);
        std::erase_if(s_pendingReceives, [&](const PendingReceive& p) { return p.handle == handle; });
        Wake();
    }

    // (caller, socket, buffers, count, bytes*, flags*, from, fromLength*) in
    // r3-r10, the overlapped and the completion routine on the stack.
    void OverlappedReceive(PPCContext& ctx, uint8_t* base, uint32_t from, uint32_t fromLength, uint32_t overlapped)
    {
        auto* socket = SocketFromHandle(ctx.r4.u32);
        if (!online::Enabled())
        {
            ctx.r3.u64 = SocketFailure(10050);  // WSAENETDOWN
            return;
        }
        if (!socket)
        {
            ctx.r3.u64 = SocketFailure(10038);
            return;
        }
        const GuestBuffers buffers = LoadBuffers(base, ctx.r5.u32, ctx.r6.u32);
        const uint32_t bytes = ctx.r7.u32, flags = ctx.r8.u32;
        const ssize_t n = ReceiveInto(base, socket->fd, buffers, from, fromLength, MSG_DONTWAIT);
        if (n >= 0)
        {
            if (bytes)
                PPC_STORE_U32(bytes, uint32_t(n));
            if (flags)
                PPC_STORE_U32(flags, 0);
            if (overlapped)
                CompleteReceive(base, overlapped, 0, uint32_t(n));
            ctx.r3.u64 = 0;
            return;
        }
        if (errno != EWOULDBLOCK || !overlapped)
        {
            ctx.r3.u64 = SocketFailure(ErrnoToWsa(errno));
            return;
        }
        static std::once_flag started;
        std::call_once(started, [base] {
            if (pipe(s_receiveWake) == 0)
            {
                fcntl(s_receiveWake[0], F_SETFL, O_NONBLOCK);
                fcntl(s_receiveWake[1], F_SETFL, O_NONBLOCK);
            }
            std::thread(ReceiveThread, base).detach();
        });
        PPC_STORE_U32(overlapped, kStatusPending);
        PPC_STORE_U32(overlapped + 4, 0);
        if (const uint32_t event = PPC_LOAD_U32(overlapped + 16))
            NtClearEvent(event);
        {
            // The same overlapped asked again: this receive replaces the one before.
            std::lock_guard lock(s_receiveMutex);
            std::erase_if(s_pendingReceives, [&](const PendingReceive& p) { return p.overlapped == overlapped; });
            s_pendingReceives.push_back({ ctx.r4.u32, buffers, from, fromLength, overlapped, socket->fd });
        }
        Wake();
        ctx.r3.u64 = SocketFailure(997);  // WSA_IO_PENDING
    }
}

PPC_FUNC(__imp__NetDll_WSARecv) { OverlappedReceive(ctx, base, 0, 0, ctx.r9.u32); }
PPC_FUNC(__imp__NetDll_WSARecvFrom) { OverlappedReceive(ctx, base, ctx.r9.u32, ctx.r10.u32, PPC_LOAD_U32(ctx.r1.u32 + 0x54)); }

// (caller, socket, overlapped, bytes*, wait, flags*): TRUE once complete.
PPC_FUNC(__imp__NetDll_WSAGetOverlappedResult)
{
    const uint32_t overlapped = ctx.r5.u32;
    if (!online::Enabled() || !overlapped)
    {
        GuestThread::SetLastError(10050);
        ctx.r3.u64 = 0;
        return;
    }
    uint32_t status = PPC_LOAD_U32(overlapped);
    if (status == kStatusPending && ctx.r7.u32)
    {
        if (const uint32_t event = PPC_LOAD_U32(overlapped + 16))
            NtWaitForSingleObjectEx(event, 1, 0, nullptr);
        status = PPC_LOAD_U32(overlapped);
    }
    if (status == kStatusPending)
    {
        GuestThread::SetLastError(996);  // WSA_IO_INCOMPLETE
        ctx.r3.u64 = 0;
        return;
    }
    if (ctx.r6.u32)
        PPC_STORE_U32(ctx.r6.u32, PPC_LOAD_U32(overlapped + 4));
    if (ctx.r8.u32)
        PPC_STORE_U32(ctx.r8.u32, 0);
    if (status != 0)
    {
        GuestThread::SetLastError(10054);  // WSAECONNRESET
        ctx.r3.u64 = 0;
        return;
    }
    ctx.r3.u64 = 1;
}

// WSA events are kernel events (manual reset), as WSAWaitForMultipleEvents
// above waits on them.
PPC_FUNC(__imp__NetDll_WSACreateEvent)
{
    be<uint32_t> handle = 0;
    ctx.r3.u64 = NtCreateEvent(&handle, nullptr, 0, 0) == 0 ? uint32_t(handle) : 0;
}
PPC_FUNC(__imp__NetDll_WSACloseEvent) { ctx.r3.u64 = NtClose(ctx.r3.u32) == 0; }
PPC_FUNC(__imp__NetDll_WSASetEvent) { ctx.r3.u64 = NtSetEvent(ctx.r3.u32, nullptr) == 0; }
PPC_FUNC(__imp__NetDll_WSAResetEvent) { ctx.r3.u64 = NtClearEvent(ctx.r3.u32) == 0; }

// Secure-association keys: accepted, never used.
PPC_FUNC(__imp__NetDll_XNetCreateKey)
{
    // (caller, XNKID* 8 bytes, XNKEY* 16 bytes)
    if (ctx.r4.u32) arc4random_buf(base + ctx.r4.u32, 8);
    if (ctx.r5.u32) arc4random_buf(base + ctx.r5.u32, 16);
    ctx.r3.u64 = 0;
}
PPC_FUNC(__imp__NetDll_XNetRegisterKey) { ctx.r3.u64 = 0; }
PPC_FUNC(__imp__NetDll_XNetUnregisterKey) { ctx.r3.u64 = 0; }
PPC_FUNC(__imp__NetDll_XNetXnAddrToInAddr)
{
    // The address's own IP (0 offline). Online a peer's XNADDR carries its
    // real IPv4 (FillXnAddr), the secure address is that one: ina, else
    // inaOnline, else loopback.
    uint32_t address = 0;
    if (ctx.r4.u32)
    {
        memcpy(&address, base + ctx.r4.u32, 4);
        if (!address && online::Enabled())
            memcpy(&address, base + ctx.r4.u32 + 4, 4);
    }
    if (!address && online::Enabled())
        address = htonl(INADDR_LOOPBACK);
    if (ctx.r6.u32)
        memcpy(base + ctx.r6.u32, &address, 4);
    ctx.r3.u64 = 0;
}
PPC_FUNC(__imp__NetDll_XNetConnect) { ctx.r3.u64 = 0; }
// XNET_CONNECT_STATUS_LOST offline, CONNECTED online.
PPC_FUNC(__imp__NetDll_XNetGetConnectStatus)
{
    NET_LOG("XNetGetConnectStatus: %s\n", online::Enabled() ? "connected" : "lost");
    ctx.r3.u64 = online::Enabled() ? 2 : 3;
}

// Lookups complete at once and find nothing: an XNDNS with an error status
// (WSAHOST_NOT_FOUND), an XNQOS with no entries; the event is set.
static uint32_t AllocZeroed(uint32_t size)
{
    void* p = g_userHeap.Alloc(size);
    if (!p) return 0;
    memset(p, 0, size);
    return g_memory.MapVirtual(p);
}

// Online too: the game would look up EA's own servers, which are gone, and
// nothing is ever asked of the outside on its behalf (the lobby server comes
// from XOnlineGetServiceInfo, kernel/xmsg.cpp).
PPC_FUNC(__imp__NetDll_XNetDnsLookup)
{
    if (s_dnsLookups++ < 4)
        fprintf(stderr, "[net] %s: DNS lookup of \"%s\" finds nothing\n", online::Enabled() ? "online" : "offline",
            ctx.r4.u32 ? reinterpret_cast<const char*>(base + ctx.r4.u32) : "");
    if (const uint32_t out = ctx.r6.u32)
    {
        const uint32_t dns = AllocZeroed(40);
        if (dns) PPC_STORE_U32(dns, 11001);  // WSAHOST_NOT_FOUND
        PPC_STORE_U32(out, dns);
    }
    if (ctx.r5.u32) NtSetEvent(ctx.r5.u32, nullptr);
    ctx.r3.u64 = 0;
}

static void ReleaseLookup(PPCContext& ctx)
{
    if (ctx.r4.u32) g_userHeap.Free(g_memory.Translate(ctx.r4.u32));
    ctx.r3.u64 = ctx.r4.u32 ? 0 : 0x80070057;
}
PPC_FUNC(__imp__NetDll_XNetDnsRelease) { ReleaseLookup(ctx); }
PPC_FUNC(__imp__NetDll_XNetQosRelease) { ReleaseLookup(ctx); }

PPC_FUNC(__imp__NetDll_XNetQosServiceLookup)
{
    NET_LOG("XNetQosServiceLookup: nothing\n");
    // (caller, flags, event, XNQOS** out)
    if (ctx.r6.u32) PPC_STORE_U32(ctx.r6.u32, AllocZeroed(48));
    if (ctx.r5.u32) NtSetEvent(ctx.r5.u32, nullptr);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetQosLookup)
{
    NET_LOG("XNetQosLookup: %u addresses, no answer\n", ctx.r4.u32);
    // (caller, cxna, apxna, apxnkid, apxnkey, cina, aina, adwServiceId,
    // cProbes, dwBitsPerSec, dwFlags, hEvent, XNQOS** out): the last five on
    // the stack. No probes answer: count 0.
    // the stack (each in an 8-byte slot from r1+0x54).
    const uint32_t event = PPC_LOAD_U32(ctx.r1.u32 + 0x6C), out = PPC_LOAD_U32(ctx.r1.u32 + 0x74);
    if (out) PPC_STORE_U32(out, AllocZeroed(48));
    if (event) NtSetEvent(event, nullptr);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XNetLogonGetTitleID) { ctx.r3.u64 = 0x454107DC; }
PPC_FUNC(__imp__XNetLogonGetMachineID)
{
    if (ctx.r3.u32) PPC_STORE_U64(ctx.r3.u32, 0xFA00000000000001ull);
    ctx.r3.u64 = 0;
}
