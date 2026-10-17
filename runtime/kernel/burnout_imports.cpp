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
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>

PPC_EXTERN_FUNC(__imp__XMsgStartIORequest);
uint32_t NtWaitForMultipleObjectsEx(uint32_t count, be<uint32_t>* handles, uint32_t waitType,
    uint32_t waitMode, uint32_t alertable, be<int64_t>* timeout);

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
    // Initialise offline. Link status below explicitly reports no cable.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetCleanup) { ctx.r3.u64 = 0; }
PPC_FUNC(__imp__NetDll_XNetGetEthernetLinkStatus) { ctx.r3.u64 = 0; }

PPC_FUNC(__imp__NetDll_XNetGetTitleXnAddr)
{
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
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ctx.r3.u64 = bind(socket->fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0
        ? SocketFailure(errno == EADDRINUSE ? 10048 : 10022) : 0;
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
    if (!socket)
        ctx.r3.u64 = SocketFailure(10038);
    else
    {
        ReleaseKernelObject(socket);
        ctx.r3.u64 = 0;
    }
}
