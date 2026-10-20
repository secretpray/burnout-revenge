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
#include <deque>
#include <map>
#include <memory>
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
    void DropPendingReceives(uint32_t handle);
    struct OfflineSocket;
    void LeavePort(OfflineSocket* socket);

    // A datagram taken off a shared port for another of its sockets.
    struct Datagram
    {
        std::vector<uint8_t> data;
        sockaddr_in from{};
    };

    // A socket. closesocket closes it (the host socket, its pending
    // receives) but leaves the object: the game also hands the handle to
    // CloseHandle afterwards (its peer link's socket, at a race's start),
    // which Xenia refuses and our NtClose would have freed a second time - the
    // heap went (both copies crashed in NtClose, 20.10). Now that NtClose frees
    // it, once; a handle closesocket closed is no socket any more.
    //
    // A UDP port can be one several sockets share (see SharePort): `share`
    // holds them in the order they bound, `peers` where this one has sent
    // (its datagrams come back to it), `queue` what came for it while another
    // was reading. Under s_receiveMutex.
    struct PortShare
    {
        std::vector<OfflineSocket*> members;
    };
    struct OfflineSocket : KernelObject
    {
        int fd;
        bool udp;
        std::shared_ptr<PortShare> share;
        std::vector<sockaddr_in> peers;
        std::deque<Datagram> queue;
        OfflineSocket(int fd, bool udp) : fd(fd), udp(udp) {}
        void Close()
        {
            if (fd < 0)
                return;
            DropPendingReceives(GetKernelHandle(this));
            LeavePort(this);
            ::close(fd);
            fd = -1;
        }
        ~OfflineSocket() override { Close(); }
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
    // A send on a connection its peer has closed fails (EPIPE, as Winsock's
    // error) instead of raising SIGPIPE, which ends the process: online, a
    // player leaving a game took every other copy down with it.
#ifdef SO_NOSIGPIPE
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    ctx.r3.u64 = GetKernelHandle(CreateKernelObject<OfflineSocket>(fd, type == SOCK_DGRAM));
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

namespace
{
    uint16_t HostPort(uint16_t guestPortNetwork);
    bool SharePort(OfflineSocket* socket, const sockaddr_in& local);
    void HoldPort(OfflineSocket* socket);
    void NoteSentTo(OfflineSocket* socket, const sockaddr_in& to);
    void ExpectPeer(OfflineSocket* socket, uint16_t port);
    void NotePlayerAddress(uint32_t address);
}

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
    // A UDP port another of the game's sockets holds is shared (SharePort)
    // before the system is asked: macOS lets a second bind through at times,
    // and then gives the port's datagrams to one of the two only - a player
    // never heard the one it had called from the new socket (four bots, 20.10).
    const bool sharing = online::Enabled() && socket->udp && local.sin_port != 0;
    bool shared = sharing && SharePort(socket, local);
    bool ok = shared || bind(socket->fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0;
    const int error = errno;
    if (ok && !shared && online::Enabled() && socket->udp)
        HoldPort(socket);
    if (ok && sharing)
        ExpectPeer(socket, local.sin_port);
    if (online::Enabled())
    {
        const uint8_t* ip = reinterpret_cast<const uint8_t*>(&local.sin_addr.s_addr);
        fprintf(stderr, "[net] bind %08X to %u.%u.%u.%u:%u%s\n", ctx.r4.u32, ip[0], ip[1], ip[2], ip[3], ntohs(local.sin_port),
            shared ? ": shares the port" : ok ? "" : " failed");
    }
    ctx.r3.u64 = ok ? 0 : SocketFailure(error == EADDRINUSE ? 10048 : 10022);
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

PPC_FUNC(__imp__NetDll_closesocket)
{
    const uint32_t handle = ctx.r4.u32;
    if (handle == 0xFFFFFFFF || !IsKernelObject(handle))
    {
        ctx.r3.u64 = SocketFailure(10038);
        return;
    }
    auto* socket = dynamic_cast<OfflineSocket*>(GetKernelObject(handle));
    if (!socket || socket->fd < 0)
        ctx.r3.u64 = SocketFailure(10038);
    else
    {
        if (socket->udp && online::Enabled() && std::getenv("BURNOUT_LOG_NET"))
            fprintf(stderr, "[net] close %08X\n", handle);
        socket->Close();
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
        auto* socket = dynamic_cast<OfflineSocket*>(GetKernelObject(handle));
        return socket && socket->fd >= 0 ? socket : nullptr;  // closed: no socket
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

    void CompleteReceive(uint8_t* base, uint32_t overlapped, uint32_t status, uint32_t bytes)
    {
        PPC_STORE_U32(overlapped + 4, bytes);
        PPC_STORE_U32(overlapped, status);
        if (const uint32_t event = PPC_LOAD_U32(overlapped + 16))
            NtSetEvent(event, nullptr);
    }

    size_t BufferSpace(const GuestBuffers& buffers)
    {
        size_t space = 0;
        for (uint32_t i = 0; i < buffers.count; i++)
            space += buffers.length[i];
        return space;
    }

    // Into the game's buffers and its address: the bytes taken.
    uint32_t CopyOut(uint8_t* base, const Datagram& d, const GuestBuffers& buffers, uint32_t from, uint32_t fromLength)
    {
        size_t done = 0;
        for (uint32_t i = 0; i < buffers.count && done < d.data.size(); i++)
        {
            const size_t part = std::min<size_t>(buffers.length[i], d.data.size() - done);
            if (buffers.address[i])
                memcpy(base + buffers.address[i], d.data.data() + done, part);
            done += part;
        }
        if (from || fromLength)
            StoreGuestAddress(base, from, fromLength, d.from);
        return uint32_t(done);
    }

    // ---- A UDP port the game's sockets share --------------------------------
    //
    // The game binds a socket per player in a game to its peer-to-peer port
    // (CommUDP ":8192:8192"). On the console the second bind succeeds and the
    // sockets share the port; here it failed, and a third player never
    // connected to the others (four bots, 20.10). Now a bind of a UDP port
    // another of the game's sockets holds takes a duplicate of that socket:
    // it sends from the port, and each datagram that arrives goes to the
    // socket for its sender's player (ExpectPeer) or that has sent to it, from
    // a sender none has to the newest socket with no one yet (Route). (The
    // finding and that last rule are Xerenge's: shipa-2/rexglue-xerenge
    // e029bb7, src/system/xsocket.cpp, GPL-3.0-or-later; the code is ours.)
    // Under s_receiveMutex.
    std::map<std::pair<uint32_t, uint16_t>, std::weak_ptr<PortShare>> s_ports;  // by address and port, network order

    std::pair<uint32_t, uint16_t> PortKey(const sockaddr_in& a) { return { a.sin_addr.s_addr, a.sin_port }; }

    bool SameAddress(const sockaddr_in& a, const sockaddr_in& b)
    {
        return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
    }

    void HoldPort(OfflineSocket* socket)
    {
        sockaddr_in local{};
        socklen_t length = sizeof(local);
        if (getsockname(socket->fd, reinterpret_cast<sockaddr*>(&local), &length) < 0)
            return;
        std::lock_guard lock(s_receiveMutex);
        socket->share = std::make_shared<PortShare>();
        socket->share->members.push_back(socket);
        s_ports[PortKey(local)] = socket->share;
    }

    bool SharePort(OfflineSocket* socket, const sockaddr_in& local)
    {
        std::lock_guard lock(s_receiveMutex);
        const auto it = s_ports.find(PortKey(local));
        const auto share = it != s_ports.end() ? it->second.lock() : nullptr;
        if (!share || share->members.empty())
            return false;
        const int fd = fcntl(share->members.front()->fd, F_DUPFD_CLOEXEC, 0);
        if (fd < 0)
            return false;
        ::close(socket->fd);
        socket->fd = fd;
        socket->share = share;
        share->members.push_back(socket);
        return true;
    }

    void LeavePort(OfflineSocket* socket)
    {
        std::lock_guard lock(s_receiveMutex);
        if (socket->share)
            std::erase(socket->share->members, socket);
        socket->share.reset();
        socket->queue.clear();
        socket->peers.clear();
    }

    void NoteSentTo(OfflineSocket* socket, const sockaddr_in& to)
    {
        if (!socket->udp)
            return;
        std::lock_guard lock(s_receiveMutex);
        if (socket->peers.size() < 32 && std::none_of(socket->peers.begin(), socket->peers.end(), [&](const sockaddr_in& p) { return SameAddress(p, to); }))
            socket->peers.push_back(to);
    }

    // Who a datagram is for: the socket whose player sent it (ExpectPeer), or
    // that has sent to its sender; from a sender none has, the newest socket
    // with no one yet, else the first.
    OfflineSocket* Route(PortShare& share, const sockaddr_in& from)
    {
        for (OfflineSocket* m : share.members)
            for (const sockaddr_in& p : m->peers)
                if (SameAddress(p, from))
                    return m;
        for (auto m = share.members.rbegin(); m != share.members.rend(); ++m)
            if ((*m)->peers.empty())
                return *m;
        return share.members.front();
    }

    // Which player a socket is for. A joining console makes one per player in
    // the game, each waiting to be called, and one given another player's
    // datagram first never answered its own - a third player never connected
    // (20.10). Right before, the game turns each player's XNADDR into an
    // address (XNetXnAddrToInAddr), in the order it then binds the sockets:
    // a UDP socket bound just after takes the oldest such address not taken.
    std::deque<std::pair<uint32_t, std::chrono::steady_clock::time_point>> s_playerAddresses;

    void NotePlayerAddress(uint32_t address)
    {
        std::lock_guard lock(s_receiveMutex);
        s_playerAddresses.emplace_back(address, std::chrono::steady_clock::now());
        while (s_playerAddresses.size() > 8)
            s_playerAddresses.pop_front();
    }

    void ExpectPeer(OfflineSocket* socket, uint16_t port)
    {
        std::lock_guard lock(s_receiveMutex);
        const auto now = std::chrono::steady_clock::now();
        while (!s_playerAddresses.empty() && now - s_playerAddresses.front().second > std::chrono::seconds(2))
            s_playerAddresses.pop_front();
        if (s_playerAddresses.empty() || !socket->share)
            return;
        sockaddr_in peer{};
        peer.sin_len = sizeof(peer);
        peer.sin_family = AF_INET;
        peer.sin_addr.s_addr = s_playerAddresses.front().first;
        peer.sin_port = port;  // the players' port is the game's own on every console
        s_playerAddresses.pop_front();
        socket->peers.push_back(peer);
        const uint8_t* ip = reinterpret_cast<const uint8_t*>(&peer.sin_addr.s_addr);
        NET_LOG("socket %08X is for the player at %u.%u.%u.%u\n", GetKernelHandle(socket), ip[0], ip[1], ip[2], ip[3]);
    }

    bool TakeFor(uint8_t* base, OfflineSocket* self, Datagram& out, size_t most);

    // A datagram queued for `to`: into its receive waiting, if it has one.
    void DeliverQueued(uint8_t* base, OfflineSocket* to)
    {
        const uint32_t handle = GetKernelHandle(to);
        const auto it = std::find_if(s_pendingReceives.begin(), s_pendingReceives.end(), [&](const PendingReceive& p) { return p.handle == handle; });
        if (it == s_pendingReceives.end() || to->queue.empty())
            return;
        const uint32_t bytes = CopyOut(base, to->queue.front(), it->buffers, it->from, it->fromLength);
        to->queue.pop_front();
        NET_LOG("received %08X: %u bytes, from the queue\n", handle, bytes);
        CompleteReceive(base, it->overlapped, 0, bytes);
        s_pendingReceives.erase(it);
    }

    // What there is for `self` (at most `most` bytes of a stream), without
    // waiting: false (errno set) when nothing. Datagrams for the port's
    // other sockets are handed to them on the way. Under s_receiveMutex.
    bool TakeFor(uint8_t* base, OfflineSocket* self, Datagram& out, size_t most)
    {
        if (!self->queue.empty())
        {
            out = std::move(self->queue.front());
            self->queue.pop_front();
            return true;
        }
        for (;;)
        {
            Datagram d;
            d.data.resize(self->udp ? 2048 : std::max<size_t>(most, 1));
            socklen_t length = sizeof(d.from);
            const ssize_t n = recvfrom(self->fd, d.data.data(), d.data.size(), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&d.from), &length);
            if (n < 0)
                return false;
            d.data.resize(size_t(n));
            if (!self->share || self->share->members.size() < 2)
            {
                out = std::move(d);
                return true;
            }
            OfflineSocket* to = Route(*self->share, d.from);
            {
                const uint8_t* ip = reinterpret_cast<const uint8_t*>(&d.from.sin_addr.s_addr);
                NET_LOG("port share: %zu bytes from %u.%u.%u.%u:%u for %08X (read by %08X)\n", d.data.size(), ip[0], ip[1], ip[2],
                    ip[3], ntohs(d.from.sin_port), GetKernelHandle(to), GetKernelHandle(self));
            }
            if (to == self)
            {
                out = std::move(d);
                return true;
            }
            if (to->queue.size() < 256)
                to->queue.push_back(std::move(d));
            DeliverQueued(base, to);
        }
    }
}

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
    // A shared port's socket may have datagrams waiting for it already, taken
    // off the port by another (none of the port's readiness): ready at once.
    bool queued[64]{};
    {
        std::lock_guard lock(s_receiveMutex);
        for (uint32_t i = 0; i < sets[0].count; ++i)
            if (auto* socket = SocketFromHandle(sets[0].handles[i]); socket && !socket->queue.empty())
                queued[i] = true, timeoutMs = 0;
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
            const bool isReady = k == 0 ? (revents & (POLLIN | POLLHUP | POLLERR)) != 0 || queued[i]
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
    // Without waiting, under the lock (a shared port's datagrams for its
    // other sockets are handed to them); a blocking socket waits outside it.
    Datagram d;
    for (;;)
    {
        bool taken;
        int error;
        {
            std::lock_guard lock(s_receiveMutex);
            taken = TakeFor(base, socket, d, ctx.r6.u32);
            error = errno;
        }
        if (taken)
            break;
        if (error != EWOULDBLOCK || (fcntl(socket->fd, F_GETFL) & O_NONBLOCK))
        {
            ctx.r3.u64 = SocketFailure(ErrnoToWsa(error));
            return;
        }
        pollfd wait{ socket->fd, POLLIN, 0 };
        poll(&wait, 1, -1);
    }
    GuestBuffers buffer;
    buffer.count = 1;
    buffer.length[0] = ctx.r6.u32;
    buffer.address[0] = ctx.r5.u32;
    const uint32_t n = CopyOut(base, d, buffer, from, fromLength);
    if (LogNet())
    {
        const uint8_t* ip = reinterpret_cast<const uint8_t*>(&d.from.sin_addr.s_addr);
        char hex[3 * 32 + 1] = "";
        for (size_t i = 0; i < d.data.size() && i < 32 && d.data.size() <= 64; i++)
            snprintf(hex + 3 * i, 4, " %02X", d.data[i]);
        NET_LOG("recv %08X: %u bytes from %u.%u.%u.%u:%u%s\n", ctx.r4.u32, n, ip[0], ip[1], ip[2], ip[3], ntohs(d.from.sin_port), hex);
    }
    ctx.r3.u64 = n;
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
        if (LogNet())
        {
            char hex[3 * 32 + 1] = "";
            for (uint32_t i = 0; i < ctx.r6.u32 && i < 32 && ctx.r6.u32 <= 64; i++)
                snprintf(hex + 3 * i, 4, " %02X", base[ctx.r5.u32 + i]);
            NET_LOG("sendto %08X: %u bytes to %u.%u.%u.%u:%u%s\n", ctx.r4.u32, ctx.r6.u32, address[0], address[1], address[2], address[3],
                uint32_t(base[to + 2]) << 8 | base[to + 3], hex);
        }
        if (online::Enabled())
        {
            // Online: where the game sends it.
            sockaddr_in target{};
            target.sin_len = sizeof(target);
            target.sin_family = AF_INET;
            memcpy(&target.sin_port, base + to + 2, 2);
            target.sin_port = HostPort(target.sin_port);
            memcpy(&target.sin_addr.s_addr, address, 4);
            NoteSentTo(socket, target);
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
#ifdef SO_NOSIGPIPE
    const int one = 1;  // as NetDll_socket's
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    StoreGuestAddress(base, ctx.r5.u32, ctx.r6.u32, peer);
    ctx.r3.u64 = GetKernelHandle(CreateKernelObject<OfflineSocket>(fd, false));
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
                // (Taking it may complete other receives, and move this one.)
                const PendingReceive pending = *it;
                auto* socket = SocketFromHandle(pending.handle);
                Datagram d;
                const bool taken = socket && TakeFor(base, socket, d, BufferSpace(pending.buffers));
                if (!taken && socket && errno == EWOULDBLOCK)
                    continue;
                const uint32_t bytes = taken ? CopyOut(base, d, pending.buffers, pending.from, pending.fromLength) : 0;
                NET_LOG("received %08X: %u bytes, waited for%s\n", pending.handle, bytes, taken ? "" : " (closed)");
                CompleteReceive(base, pending.overlapped, taken ? 0 : 0xC000020C /* STATUS_CONNECTION_DISCONNECTED */, bytes);
                std::erase_if(s_pendingReceives, [&](const PendingReceive& p) { return p.overlapped == pending.overlapped; });
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
        Datagram d;
        bool taken;
        int error;
        {
            std::lock_guard lock(s_receiveMutex);
            taken = TakeFor(base, socket, d, BufferSpace(buffers));
            error = errno;
        }
        const ssize_t n = taken ? ssize_t(CopyOut(base, d, buffers, from, fromLength)) : -1;
        errno = error;
        if (n >= 0)
        {
            NET_LOG("received %08X: %zd bytes, at once%s\n", ctx.r4.u32, n, overlapped ? "" : " (no overlapped)");
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
            NET_LOG("waiting %08X: overlapped %08X, fd %d, %zu bytes\n", ctx.r4.u32, overlapped, socket->fd, BufferSpace(buffers));
            // A datagram another of the port's sockets took for this one
            // since TakeFor above: the port shows nothing more, so it is
            // handed over here, or the receive would wait for the next.
            DeliverQueued(base, socket);
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
    // A failure's code is WSAGetLastError's (SocketFailure), not only the
    // thread's: DirtySock asks WSAGetLastError whether a receive is merely
    // incomplete (996) and was told the 997 of the WSARecvFrom before it, took
    // the receive for failed and never received on that socket again - a
    // player's link on port 8192 went deaf after its first datagrams, and a
    // late reply left a fourth player out of the game (20.10).
    if (!online::Enabled() || !overlapped)
    {
        SocketFailure(10050);
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
        SocketFailure(996);  // WSA_IO_INCOMPLETE
        ctx.r3.u64 = 0;
        return;
    }
    if (ctx.r6.u32)
        PPC_STORE_U32(ctx.r6.u32, PPC_LOAD_U32(overlapped + 4));
    if (ctx.r8.u32)
        PPC_STORE_U32(ctx.r8.u32, 0);
    if (status != 0)
    {
        SocketFailure(10054);  // WSAECONNRESET
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
    if (online::Enabled() && address != htonl(INADDR_LOOPBACK))
        NotePlayerAddress(address);  // a socket for this player may follow
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
