// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Imports Burnout Revenge links but no tested run has reached, which were
// fatal stubs: the CRT's narrow formatting and debug print, the L2 lock and
// a spinlock try, the title's execution id, XAM's own heap, and two message
// boxes. Each now does what Xenia's kernel does (as rexglue-xerenge, BSD-3,
// implements them: xboxkrnl_strings, xboxkrnl_memory, xboxkrnl_threading,
// xam_info, xam_ui), written here on this runtime's own helpers, so a path
// the tests haven't taken (a debug message, a damaged file) doesn't stop the
// game. The network and Xbox Live imports stay stubs: offline they're
// never reached, and answering them "successfully" would only hide that.
#include <stdafx.h>
#include "function.h"
#include "guest_wformat.h"
#include "xam.h"

#include <SDL3/SDL_messagebox.h>

#include <atomic>
#include <cstdlib>
#include <string>

namespace
{
    // A variadic guest call's arguments from the `index`-th: the first eight
    // in r3..r10, the rest in 8-byte slots from r1 + 0x54 (the Xbox 360
    // calling convention, as Xenia's StackArgList reads it). A double is
    // passed as its bits, in a GPR or a slot like any other.
    struct VarArgs
    {
        PPCContext& ctx;
        uint8_t* base;
        int index;

        uint64_t operator()()
        {
            const PPCRegister* regs[] = { &ctx.r3, &ctx.r4, &ctx.r5, &ctx.r6, &ctx.r7, &ctx.r8, &ctx.r9, &ctx.r10 };
            const uint64_t value = index < 8 ? regs[index]->u64 : PPC_LOAD_U64(ctx.r1.u32 + 0x54 + (index - 8) * 8);
            ++index;
            return value;
        }
    };

    // A guest va_list: 8-byte big-endian slots.
    struct ListArgs
    {
        uint8_t* base;
        uint32_t address;

        uint64_t operator()()
        {
            const uint64_t value = PPC_LOAD_U64(address);
            address += 8;
            return value;
        }
    };

    std::string GuestBytes(uint8_t* base, uint32_t address)
    {
        std::string text;
        for (size_t i = 0; address && i < 65536 && base[address + i]; ++i)
            text += char(base[address + i]);
        return text;
    }

    auto StringReader(uint8_t* base)
    {
        return [base](uint32_t address, bool narrow) {
            if (!address) return std::u16string(u"(null)");
            std::u16string text;
            for (size_t i = 0; i < 65536; ++i)
            {
                const uint16_t unit = narrow ? base[address + i] : uint16_t(PPC_LOAD_U16(address + i * 2));
                if (!unit) break;
                text += char16_t(unit);
            }
            return text;
        };
    }

    template<class Args>
    bool FormatNarrow(uint8_t* base, uint32_t format, Args args, std::string& output)
    {
        const bool ok = format && guestformat::Narrow(GuestBytes(base, format), args, StringReader(base), output);
        if (!ok)
            fprintf(stderr, "[import] unsupported narrow format at %08X: \"%s\"\n", format, GuestBytes(base, format).c_str());
        return ok;
    }
}

// int sprintf(char* buffer, const char* format, ...)
PPC_FUNC(__imp__sprintf)
{
    const uint32_t buffer = ctx.r3.u32;
    std::string text;
    const bool ok = FormatNarrow(base, ctx.r4.u32, VarArgs{ ctx, base, 2 }, text);
    if (buffer)
    {
        if (ok) memcpy(base + buffer, text.data(), text.size());
        base[buffer + (ok ? text.size() : 0)] = 0;
    }
    ctx.r3.s64 = ok ? int64_t(text.size()) : -1;
}

// int _vsnprintf(char* buffer, size_t count, const char* format, va_list args):
// Microsoft's: at most `count` bytes; the terminator only if it fits; -1 when
// the text didn't fit.
PPC_FUNC(__imp___vsnprintf)
{
    const uint32_t buffer = ctx.r3.u32;
    const uint32_t count = ctx.r4.u32;
    std::string text;
    const bool ok = FormatNarrow(base, ctx.r5.u32, ListArgs{ base, ctx.r6.u32 }, text);
    if (!buffer || !ok)
    {
        if (buffer && count) base[buffer] = 0;
        ctx.r3.s64 = -1;
        return;
    }
    const size_t copied = std::min<size_t>(text.size(), count);
    memcpy(base + buffer, text.data(), copied);
    if (copied < count) base[buffer + copied] = 0;
    ctx.r3.s64 = text.size() <= count ? int64_t(text.size()) : -1;
}

// ULONG DbgPrint(const char* format, ...): into the log.
PPC_FUNC(__imp__DbgPrint)
{
    std::string text;
    if (FormatNarrow(base, ctx.r3.u32, VarArgs{ ctx, base, 1 }, text))
    {
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        fprintf(stderr, "[guest] %s\n", text.c_str());
    }
    ctx.r3.u64 = 0;  // STATUS_SUCCESS
}

// The L2 cache lock XMemCpy's streaming copies take: nothing to lock here.
PPC_FUNC(__imp__KeLockL2) { ctx.r3.u64 = 0; }
PPC_FUNC(__imp__KeUnlockL2) {}

// BOOLEAN KeTryToAcquireSpinLockAtRaisedIrql(PKSPIN_LOCK): one try of
// KeAcquireSpinLockAtRaisedIrql's (imports_unleashed.cpp), the owner being
// the calling thread's r13.
uint32_t KeTryToAcquireSpinLockAtRaisedIrql(uint32_t* spinLock)
{
    std::atomic_ref lock(*spinLock);
    uint32_t expected = 0;
    return lock.compare_exchange_strong(expected, g_ppcContext->r13.u32) ? 1 : 0;
}
GUEST_FUNCTION_HOOK(__imp__KeTryToAcquireSpinLockAtRaisedIrql, KeTryToAcquireSpinLockAtRaisedIrql);

// DWORD XamGetExecutionId(XEX_EXECUTION_ID** id): the title's execution
// info, as its XEX header holds it. Burnout Revenge USA: title 454107DC,
// disc 1 of 1; the media id and versions aren't used offline (0).
uint32_t XamGetExecutionId(be<uint32_t>* out)
{
    static const uint32_t info = [] {
        auto* p = static_cast<uint8_t*>(g_userHeap.Alloc(24));
        memset(p, 0, 24);
        *reinterpret_cast<be<uint32_t>*>(p + 12) = 0x454107DC;  // title id
        p[18] = 1;  // disc number
        p[19] = 1;  // disc count
        return g_memory.MapVirtual(p);
    }();
    if (!out) return 0x80070057;  // E_INVALIDARG
    *out = info;
    return 0;
}
GUEST_FUNCTION_HOOK(__imp__XamGetExecutionId, XamGetExecutionId);

// DWORD XamAlloc(DWORD flags, DWORD size, void** out) / XamFree(void*):
// XAM's own allocations (the network stack's), zeroed, from the title heap.
uint32_t XamAlloc(uint32_t flags, uint32_t size, be<uint32_t>* out)
{
    if (!out) return 0x80070057;
    void* p = size ? g_userHeap.Alloc(size) : nullptr;
    if (!p && size)
    {
        *out = 0;
        return 0x8007000E;  // E_OUTOFMEMORY
    }
    if (p) memset(p, 0, size);
    *out = p ? g_memory.MapVirtual(p) : 0;
    return 0;
}
GUEST_FUNCTION_HOOK(__imp__XamAlloc, XamAlloc);

uint32_t XamFree(uint32_t address)
{
    if (address) g_userHeap.Free(g_memory.Translate(address));
    return 0;
}
GUEST_FUNCTION_HOOK(__imp__XamFree, XamFree);

// The console's "disc unreadable" screen, then out, as there: the game
// couldn't read one of its files.
uint32_t XamShowDirtyDiscErrorUI(uint32_t userIndex)
{
    fprintf(stderr, "[import] XamShowDirtyDiscErrorUI: the game couldn't read one of its files; quitting\n");
    fflush(stderr);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Burnout Revenge",
        "The game couldn't read one of its files, as a scratched disc would on the console.\n\n"
        "Install the game again from your disc image: remove the game folder in "
        "~/Library/Application Support/BurnoutRevenge and open the app.", nullptr);
    std::_Exit(1);
}
GUEST_FUNCTION_HOOK(__imp__XamShowDirtyDiscErrorUI, XamShowDirtyDiscErrorUI);

// XamShowMessageBoxUIEx: XamShowMessageBoxUI's arguments with one more (unused)
// after the flags, so its result pointer and overlapped are the 9th and 10th,
// on the stack.
PPC_FUNC(__imp__XamShowMessageBoxUIEx)
{
    VarArgs args{ ctx, base, 0 };
    const uint32_t user = uint32_t(args()), title = uint32_t(args()), text = uint32_t(args()), buttons = uint32_t(args()),
                   labels = uint32_t(args()), focus = uint32_t(args()), flags = uint32_t(args());
    args();  // unused
    const uint32_t result = uint32_t(args()), overlapped = uint32_t(args());
    auto at = [base](uint32_t address) { return address ? base + address : nullptr; };
    ctx.r3.u64 = XamShowMessageBoxUI(user, reinterpret_cast<be<uint16_t>*>(at(title)), reinterpret_cast<be<uint16_t>*>(at(text)),
        buttons, reinterpret_cast<xpointer<be<uint16_t>>*>(at(labels)), focus, flags, reinterpret_cast<be<uint32_t>*>(at(result)),
        reinterpret_cast<XXOVERLAPPED*>(at(overlapped)));
}
