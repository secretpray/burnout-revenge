// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Links the real RTL imports and XMsg dispatcher, replacing only host services
// (heap, memory, music decoder, VFS and notifications). Calls guest ABI wrappers
// too; no game files, window, 4 GB reservation or audio device are needed.
#include <stdafx.h>
#include <kernel/function.h>
#include <kernel/rtl_strings.h>
#include <kernel/notification_queue.h>
#include <kernel/overlapped.h>
#include <cpu/guest_thread.h>
#include <kernel/vfs.h>
#include <apu/music.h>
#include <functional>
#include <unordered_set>

uint32_t XMsgSystemProcessCall(uint32_t, uint32_t, uint32_t, uint32_t);
PPC_EXTERN_FUNC(__imp__RtlUnicodeStringToAnsiString);
PPC_EXTERN_FUNC(__imp__RtlFreeAnsiString);

alignas(32) static std::array<uint8_t, 512 * 1024> memory;
Memory::Memory() {}
Memory g_memory;
Heap g_userHeap;
static size_t heapNext = 0x40000;
static bool failAllocation = false;
static std::unordered_set<void*> allocations;
static uint32_t frees = 0;
void* Heap::Alloc(size_t size)
{
    if (failAllocation) return nullptr;
    assert(heapNext + size <= memory.size());
    void* p = memory.data() + heapNext;
    heapNext += (size + 15) & ~size_t(15);
    allocations.insert(p);
    return p;
}
void Heap::Free(void* p) { assert(allocations.erase(p) == 1); ++frees; }
bool ImportTraceEnabled() { return false; }
void ImportTraceCall(const char*, const PPCContext&) {}
void ImportTraceReturn(const char*, const PPCContext&) {}
void GuestThread::SetLastError(uint32_t) {}
void CompleteOverlapped(XXOVERLAPPED*, uint32_t, uint32_t, uint32_t) { assert(false); }
namespace vfs
{
    std::optional<Resolved> Resolve(std::string_view path, bool) { return Resolved{std::filesystem::path(path), true}; }
}

static xnotify::Queue notifications;
void XamNotifyEnqueueEvent(uint32_t id, uint32_t data) { notifications.Push(id, data); }
static std::function<void(uint64_t, bool)> songEnd;
static uint64_t songNumber = 0;
static uint32_t plays = 0, resumes = 0;
namespace apu::music
{
    uint64_t Play(const std::filesystem::path&) { ++plays; return ++songNumber; }
    void Stop() {}
    void Pause() {}
    void Resume() { ++resumes; }
    void SetVolume(float) {}
    void SetSongEndHandler(std::function<void(uint64_t, bool)> f) { songEnd = std::move(f); }
}

template<class T> T* At(uint32_t address) { return reinterpret_cast<T*>(memory.data() + address); }
static X_UNICODE_STRING Source(std::u16string_view s)
{
    for (size_t i = 0; i < s.size(); ++i) At<be<uint16_t>>(0x1000)[i] = s[i];
    return {uint16_t(s.size() * 2), uint16_t(s.size() * 2), 0x1000};
}

static void TestRtl()
{
    auto* src = At<X_UNICODE_STRING>(0x100);
    auto* dst = At<X_ANSI_STRING>(0x200);
    *src = Source(u"GAME:\\eatrax\\01.wma");
    PPCContext ctx{};
    ctx.r3.u32 = 0x200; ctx.r4.u32 = 0x100; ctx.r5.u32 = 1;
    __imp__RtlUnicodeStringToAnsiString(ctx, memory.data());
    assert(ctx.r3.u32 == STATUS_SUCCESS);
    assert(dst->length == 19 && dst->maximumLength == 20);
    assert(std::string(At<char>(dst->buffer)) == "GAME:\\eatrax\\01.wma");
    // Layout in guest RAM, not just host field values.
    assert(memory[0x200] == 0 && memory[0x201] == 19 && memory[0x203] == 20);
    ctx.r3.u32 = 0x200;
    __imp__RtlFreeAnsiString(ctx, memory.data());
    assert(dst->buffer == 0 && dst->length == 0 && dst->maximumLength == 0 && frees == 1);
    RtlFreeAnsiString(dst); RtlFreeAnsiString(nullptr);
    assert(frees == 1);

    *src = Source(u"éЖ\U0001F697");
    assert(RtlUnicodeStringToAnsiString(dst, src, 1) == STATUS_SUCCESS);
    const std::string utf8 = "\xC3\xA9\xD0\x96\xF0\x9F\x9A\x97";
    assert(dst->length == utf8.size() && dst->maximumLength == utf8.size() + 1);
    assert(std::string(At<char>(dst->buffer)) == utf8);
    RtlFreeAnsiString(dst);
    for (uint16_t capacity = 1; capacity <= 9; ++capacity)
    {
        *dst = {0, capacity, 0x3001};
        const uint16_t copied = capacity >= 9 ? 8 : capacity >= 5 ? 4 : capacity >= 3 ? 2 : 0;
        assert(RtlUnicodeStringToAnsiString(dst, src, 0) ==
            (capacity >= 9 ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW));
        assert(dst->length == copied && memcmp(At<char>(0x3001), utf8.data(), copied) == 0);
        assert(memory[0x3001 + copied] == 0);
    }
    *src = Source(std::u16string_view(u"A\0B", 3));
    assert(RtlUnicodeStringToAnsiString(dst, src, 1) == STATUS_SUCCESS);
    assert(dst->length == 3 && memcmp(At<char>(dst->buffer), "A\0B\0", 4) == 0);
    RtlFreeAnsiString(dst);

    *src = Source(u"ABCDE");
    *dst = {0, 6, 0x3001};
    ctx.r3.u32 = 0x200; ctx.r4.u32 = 0x100; ctx.r5.u32 = 0x100; // BOOLEAN low byte is false
    __imp__RtlUnicodeStringToAnsiString(ctx, memory.data());
    assert(ctx.r3.u32 == STATUS_SUCCESS && dst->buffer == 0x3001 && allocations.empty());
    for (uint16_t capacity = 0; capacity <= 7; ++capacity)
    {
        memset(At<char>(0x3000), 0x7F, 10);
        *dst = {0, capacity, 0x3001};
        auto status = RtlUnicodeStringToAnsiString(dst, src, 0);
        assert(status == (capacity < 6 ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS));
        size_t copied = capacity ? std::min<size_t>(5, capacity - 1) : 0;
        assert(dst->length == (capacity ? copied : 5));
        assert(memory[0x3000] == 0x7F && memory[0x3001 + capacity] == 0x7F);
        if (capacity)
        {
            assert(memcmp(At<char>(0x3001), "ABCDE", copied) == 0);
            assert(memory[0x3001 + copied] == 0);
        }
        assert(dst->buffer == 0x3001 && dst->maximumLength == capacity);
    }
    *dst = {9, 10, 0x3001};
    const auto before = *dst;
    failAllocation = true;
    assert(RtlUnicodeStringToAnsiString(dst, src, 1) == STATUS_NO_MEMORY);
    assert(memcmp(dst, &before, sizeof(before)) == 0);
    failAllocation = false;
    src->length = 3;
    assert(RtlUnicodeStringToAnsiString(dst, src, 1) == STATUS_INVALID_PARAMETER_2);
    *src = Source(u"A"); src->maximumLength = 0;
    assert(RtlUnicodeStringToAnsiString(dst, src, 1) == STATUS_INVALID_PARAMETER_2);
    *src = Source(u"A"); src->buffer = 0;
    assert(RtlUnicodeStringToAnsiString(dst, src, 1) == STATUS_INVALID_PARAMETER_2);
    assert(RtlUnicodeStringToAnsiString(dst, nullptr, 1) == STATUS_INVALID_PARAMETER_2);
    assert(RtlUnicodeStringToAnsiString(nullptr, src, 1) == STATUS_INVALID_PARAMETER);
    *src = Source(std::u16string(1, char16_t(0xD800)));
    assert(RtlUnicodeStringToAnsiString(dst, src, 1) == STATUS_ILLEGAL_CHARACTER);
    *src = Source(std::u16string(1, char16_t(0xDC00)));
    assert(RtlUnicodeStringToAnsiString(dst, src, 1) == STATUS_ILLEGAL_CHARACTER);
    *src = Source(std::u16string(22000, u'\u0800')); // UTF-8 result cannot fit USHORT
    assert(RtlUnicodeStringToAnsiString(dst, src, 1) == STATUS_INVALID_PARAMETER_2);
    // Last UTF-16 unit in accessible memory, with no NUL afterwards.
    *At<be<uint16_t>>(uint32_t(memory.size() - 2)) = 'Z';
    *src = {2, 2, uint32_t(memory.size() - 2)};
    assert(RtlUnicodeStringToAnsiString(dst, src, 1) == STATUS_SUCCESS);
    assert(dst->length == 1 && std::string(At<char>(dst->buffer)) == "Z");
    RtlFreeAnsiString(dst);
    *src = {};
    assert(RtlUnicodeStringToAnsiString(dst, src, 1) == STATUS_SUCCESS);
    assert(dst->length == 0 && dst->maximumLength == 1 && *At<char>(dst->buffer) == 0);
    RtlFreeAnsiString(dst);
    *dst = {0, 1, 0};
    assert(RtlUnicodeStringToAnsiString(dst, src, 0) == STATUS_INVALID_PARAMETER);
    assert(allocations.empty());
}

static uint32_t Message(uint32_t id, std::initializer_list<uint32_t> args = {})
{
    std::fill_n(At<be<uint32_t>>(0x100), 16, be<uint32_t>(0));
    size_t i = 0;
    for (auto arg : args) At<be<uint32_t>>(0x100)[i++] = arg;
    return XMsgSystemProcessCall(0xFA, id, 0x100, uint32_t(args.size() * 4));
}
static uint32_t State()
{
    assert(Message(0x70009, {0, 0x500}) == 0);
    return *At<be<uint32_t>>(0x500);
}
static void TestXmp()
{
    constexpr uint32_t changed = 0x0A000001;
    assert(Message(0x7000D, {0, 0x400, 4, 0, 0, 0, 0, 0, 0x480}) == 0);
    assert(Message(0x70002, {0, 0x400, 0}) == 0);
    assert(State() == 0 && plays == 0); // regression: used to become Playing
    assert(notifications.Pop() == xnotify::Queue::Event(changed, 0));
    auto count = notifications.enqueued;
    for (int i = 0; i < 60000; ++i) assert(Message(0x70003) == 0);
    assert(State() == 0 && notifications.enqueued == count && resumes == 0);
    // Two songs: end of first must notify even though state stays Playing.
    memset(At<uint8_t>(0x800), 0, 72);
    assert(Message(0x7000D, {0, 0x400, 260, 0x800, 2, 0, 0, 0x600, 0x480}) == 0);
    assert(Message(0x70008, {0, 0, 1, 0}) == 0); // no repeat
    notifications = {};
    assert(Message(0x70002, {0, 0x400, 0}) == 0 && State() == 1 && plays == 1);
    assert(notifications.Pop() == xnotify::Queue::Event(changed, 1));
    count = notifications.enqueued;
    for (int i = 0; i < 60000; ++i) assert(Message(0x70003) == 0);
    assert(State() == 1 && notifications.enqueued == count && notifications.Size() == 0);
    assert(Message(0x70005) == 0 && State() == 2);
    assert(notifications.Pop() == xnotify::Queue::Event(changed, 2));
    for (int i = 0; i < 60000; ++i) assert(Message(0x70003) == 0);
    assert(State() == 1 && resumes == 1 && notifications.Size() == 1);
    assert(notifications.Pop() == xnotify::Queue::Event(changed, 1));
    const auto oldSong = songNumber;
    songEnd(songNumber, false);
    assert(State() == 1 && plays == 2);
    assert(notifications.Pop() == xnotify::Queue::Event(changed, 1));
    songEnd(oldSong, false); // stale callback must not finish the new song
    assert(State() == 1 && notifications.Size() == 0);
    songEnd(songNumber, false);
    assert(State() == 0 && notifications.Pop() == xnotify::Queue::Event(changed, 0));
    // Decoder failure also eventually stops a playlist, even in repeat mode.
    assert(Message(0x70008, {0, 0, 0, 0}) == 0);
    assert(Message(0x70002, {0, 0x400, 0}) == 0);
    songEnd(songNumber, true); songEnd(songNumber, true);
    assert(State() == 0);
    assert(Message(0x70013, {0, 0x400}) == 0);
    // Nonzero song count but no descriptor pointer must also remain idle.
    assert(Message(0x7000D, {0, 0x400, 260, 0, 2, 0, 0, 0, 0x480}) == 0);
    assert(Message(0x70002, {0, 0x400, 0}) == 0 && State() == 0);
    printf("XMP dispatcher: 180000 repeated Continue calls, no redundant events; peak=%zu\n", notifications.highWater);
}

int main()
{
    g_memory.base = memory.data();
    TestRtl();
    TestXmp();
    puts("kernel_compat_test: real RTL exports and XMsg dispatcher passed");
}
