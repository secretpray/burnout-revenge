// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Xbox Live compatibility: unsupported social services and local session objects.
// Signatures checked against Xenia xam_user.cc (BSD, see CREDITS.md) and the
// game's own PPC callers. No requests are sent to Xbox Live.
#include <stdafx.h>
#include "function.h"
#include "overlapped.h"
#include "xdm.h"

// Xbox Live's own screens a game of the lobby's can open on a player: the
// gamer card, the player review, the friends list, a message to them. There
// is no Xbox Live to show them, so nothing is shown and the game carries on
// (as the screen closing at once). These were stubs, which end the game:
// pressing the button for a player's card in an online room did (19.10).
namespace
{
    uint32_t NoLiveScreen(const char* name)
    {
        fprintf(stderr, "[online] %s: no Xbox Live screen to show; nothing shown\n", name);
        return 0;  // ERROR_SUCCESS
    }
}

uint32_t XamShowGamerCardUIForXUID(uint32_t userIndex, uint64_t xuid, uint32_t flags) { return NoLiveScreen("XamShowGamerCardUIForXUID"); }
uint32_t XamShowPlayerReviewUI(uint32_t userIndex, uint64_t xuid) { return NoLiveScreen("XamShowPlayerReviewUI"); }
uint32_t XamShowFriendsUI(uint32_t userIndex) { return NoLiveScreen("XamShowFriendsUI"); }
uint32_t XamShowMessageComposeUI(uint32_t userIndex, uint32_t count, uint32_t recipients, uint32_t text)
{
    return NoLiveScreen("XamShowMessageComposeUI");
}

// XamUserAreUsersFriends(user, xuids, count, result*, overlapped): nobody is
// a friend - there is no friends list without Xbox Live.
uint32_t XamUserAreUsersFriends(uint32_t userIndex, uint32_t xuids, uint32_t count, be<uint32_t>* result, XXOVERLAPPED* overlapped)
{
    const uint32_t status = userIndex >= 4 ? 87 : userIndex ? 1317 : 0;
    if (result)
    {
        *result = 0;
        return status;
    }
    if (overlapped)
    {
        // Async friendship results belong in Length, not an optional BOOL*.
        CompleteOverlapped(overlapped, status ? 1627 : 0, status ? 0x80070000 | status : 0, 0);
        return 997;  // ERROR_IO_PENDING
    }
    return 87;  // ERROR_INVALID_PARAMETER: no way to receive the result
}

GUEST_FUNCTION_HOOK(__imp__XamShowGamerCardUIForXUID, XamShowGamerCardUIForXUID);
GUEST_FUNCTION_HOOK(__imp__XamShowPlayerReviewUI, XamShowPlayerReviewUI);
GUEST_FUNCTION_HOOK(__imp__XamShowFriendsUI, XamShowFriendsUI);
GUEST_FUNCTION_HOOK(__imp__XamShowMessageComposeUI, XamShowMessageComposeUI);
GUEST_FUNCTION_HOOK(__imp__XamUserAreUsersFriends, XamUserAreUsersFriends);

// A player's gamer picture: there is none to read without Xbox Live. The key
// parses to nothing, and reading a tile fails (the game then draws without
// it). ABI checked against Xenia xam_user.cc and guest sub_825AEBE0.
uint32_t XamParseGamerTileKey(uint32_t key, be<uint32_t>* titleId, be<uint32_t>* bigId, be<uint32_t>* smallId)
{
    if (titleId)
        *titleId = 0;
    if (bigId)
        *bigId = 0;
    if (smallId)
        *smallId = 0;
    return 0;
}

// Eight arguments: the game passes the completion in r10, after height in r9.
// Omitting userIndex used to interpret height as an XXOVERLAPPED pointer.
uint32_t XamReadTileToTexture(uint32_t type, uint32_t titleId, uint64_t tileId, uint32_t userIndex, uint32_t buffer,
    uint32_t stride, uint32_t height, XXOVERLAPPED* overlapped)
{
    static std::atomic_flag logged = ATOMIC_FLAG_INIT;
    if (!logged.test_and_set(std::memory_order_relaxed))
    {
        fprintf(stderr, "[online] gamer picture asked for: none (no Xbox Live)\n");
    }
    if (overlapped)
    {
        CompleteOverlapped(overlapped, 2, 0x80070002);  // ERROR_FILE_NOT_FOUND
        return 997;  // ERROR_IO_PENDING
    }
    return 2;
}

GUEST_FUNCTION_HOOK(__imp__XamParseGamerTileKey, XamParseGamerTileKey);
GUEST_FUNCTION_HOOK(__imp__XamReadTileToTexture, XamReadTileToTexture);

// Xbox Live sessions (online, BURNOUT_ONLINE): the game makes a handle,
// asks for the object behind it, passes that to XGI's session messages
// (kernel/xmsg.cpp: create, join, start, end, delete) and closes the handle.
// Nothing reads the object but those messages, so it is plain zeroed guest
// memory. As Xenia's XamSessionCreateHandle/XamSessionRefObjByHandle.
namespace
{
    struct XamSession : KernelObject
    {
        uint32_t guest = 0;
        ~XamSession() override
        {
            if (guest)
                g_userHeap.Free(g_memory.Translate(guest));
        }
    };
}

uint32_t XamSessionCreateHandle(be<uint32_t>* handle)
{
    if (!handle) return 0xC000000D;  // STATUS_INVALID_PARAMETER
    *handle = 0;
    void* guest = g_userHeap.Alloc(0x100);
    if (!guest) return 0xC0000017;  // STATUS_NO_MEMORY
    // Allocate the object directly: CreateKernelObject's placement new assumes
    // success. A failed allocation must not create a successful null session.
    void* storage = g_userHeap.AllocPhysical(sizeof(XamSession), alignof(XamSession));
    if (!storage)
    {
        g_userHeap.Free(guest);
        return 0xC0000017;
    }
    auto* session = new (storage) XamSession();
    memset(guest, 0, 0x100);
    session->guest = g_memory.MapVirtual(guest);
    *handle = GetKernelHandle(session);
    fprintf(stderr, "[online] session handle %08X (object %08X)\n", uint32_t(*handle), session->guest);
    return 0;
}

uint32_t XamSessionRefObjByHandle(uint32_t handle, be<uint32_t>* object)
{
    if (!object) return 0xC000000D;
    *object = 0;
    auto* session = handle != GUEST_INVALID_HANDLE_VALUE && IsKernelObject(handle)
        ? dynamic_cast<XamSession*>(GetKernelObject(handle)) : nullptr;
    if (!session)
        return 0xC0000008;  // STATUS_INVALID_HANDLE
    *object = session->guest;
    return 0;
}

GUEST_FUNCTION_HOOK(__imp__XamSessionCreateHandle, XamSessionCreateHandle);
GUEST_FUNCTION_HOOK(__imp__XamSessionRefObjByHandle, XamSessionRefObjByHandle);
