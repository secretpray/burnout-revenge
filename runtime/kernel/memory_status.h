// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Whether the game got the memory it needs, and what to tell the player when
// it didn't. The recompiled code addresses guest memory as base + a 32-bit
// guest address, so the game needs one contiguous 4 GB of host address space
// (kernel/memory.cpp), with the 360's 512 MB of physical memory mapped three
// times inside it (0xA0000000, 0xC0000000, 0xE0000000). There is no smaller
// layout: the game's heaps reach 0x7EFFFFFF, its image sits at 0x82000000 and
// the three physical windows end at 0xFFFFFFFF.
//
// iOS limits an app's address space to 17.4 GB on a device with more than
// 3 GB of memory, about 5 GB of it free in one piece above the system's
// shared region, unless it has Extended Virtual Addressing (then the 4 GB
// lands at 0x7000000000) or Increased Memory Limit on a device that supports
// it (63 GB since iOS 16: xnu's kern_exec.c). Increased Memory Limit also
// raises the memory limit (about 3.3 GB -> 6 GB on an 8 GB iPhone), which the
// game's peak footprint (about 2.5-3 GB, scripts/ios/README.md, "Memory")
// needs on smaller devices. scripts/ios/BurnoutRevenge/BurnoutRevenge.entitlements
// asks for both. A sideloaded copy (the IPA, scripts/ios/make_ipa.sh) is
// re-signed with the player's own Apple Account: a free one never gets
// Extended Virtual Addressing, and keeps Increased Memory Limit only with
// some tools. When the 4 GB can't be had the game can't start: main.cpp shows
// StartupFailure() on screen instead of quitting. With a low memory limit it
// starts, and LowMemoryNotice() says iOS may close it.
//
// Everything here is pure (tests/memory_status_test.cpp) except Gather(),
// which reads the device.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace memstatus
{
    // Which step of Memory::Memory() failed (kernel/memory.h: failedStep).
    enum class Failure : uint8_t { None, AddressSpace, PhysicalMemory };

    // From the app's own code signature (platform/code_signature.h):
    // Unknown when it couldn't be read (or isn't iOS's concern).
    enum class Flag : uint8_t { Unknown, No, Yes };

    struct Status
    {
        Failure failure = Failure::None;
        int error = 0;               // errno for the reservation, a kern_return_t for the mirrors
        std::string errorText;       // strerror / mach_error_string
        uint64_t base = 0;           // host address of the guest's 4 GB (0: none)
        Flag extendedVirtualAddressing = Flag::Unknown;
        Flag increasedMemoryLimit = Flag::Unknown;
        bool signatureRead = false;  // the entitlements above come from the signature
        int64_t memoryLimitMB = -1;  // iOS: footprint + os_proc_available_memory() at startup; -1 unknown
        // iOS: the top of the app's address space (task_vm_info's
        // max_address), which shows whether the entitlements took effect:
        // 0xFC0000000 (63 GB) or more with them, 0x458000000 (17.4 GB)
        // without on a device with more than 3 GB; 0 unknown.
        uint64_t addressSpaceTop = 0;
        bool ios = false;
    };

    // The game's peak memory footprint with room to spare (measured 2.2 GB
    // in a race at 1x on a Mac, about 2.5 GB expected in an iPhone's free
    // roam: scripts/ios/README.md, "Memory"): below this limit iOS may
    // close the game in a busy scene.
    constexpr int64_t kPeakFootprintMB = 3072;
    // An address space that ends below this (MACH_VM_JUMBO_ADDRESS, 63 GB)
    // is iOS's default one: neither entitlement took effect, and the game's
    // 4 GB plus the 3 GB or so it needs besides (the graphics driver's,
    // its heaps) must fit in the 7 GB or so iOS leaves an app.
    constexpr uint64_t kLargeAddressSpace = 0xFC0000000;

    // The log's line: "[memory] guest 4 GB at 0x7000000000; ...".
    std::string LogLine(const Status& status);

    // The system summary's Memory item (the same facts, without the prefix).
    std::string SummaryItem(const Status& status);

    struct Message
    {
        std::string title;
        std::vector<std::string> paragraphs;  // the first says what happened, the rest what to do
        std::string details;                  // small print: the error and the entitlements
    };
    // What the player sees when the game can't start (failure != None).
    Message StartupFailure(const Status& status);

    // A one-line notice (a toast) when the game started but iOS may close
    // it: iOS's default address space (below kLargeAddressSpace), or a
    // memory limit below kPeakFootprintMB. "" otherwise (and off iOS).
    std::string LowMemoryNotice(const Status& status);

    // The device's side (iOS): the entitlements from the app's signature,
    // the memory limit and the address space's top; elsewhere only the
    // error's text (BURNOUT_TEST_MEMORY_IOS=1: the iOS wording); `base`, the
    // failure and its error come from g_memory. Main thread, once, early.
    void Gather(Status& status);

    // iOS, main thread, every frame (video::RunFrame): once a minute a log
    // line with the app's memory footprint, its peak and what is left before
    // iOS's limit ("[memory] footprint 2410 MB, peak 2511 MB, 3626 MB left"),
    // so a log cut short by iOS closing the game shows how close it was.
    // Nothing elsewhere.
    void Poll();

    const char* FlagName(Flag flag);  // "yes", "no", "unknown"
}
