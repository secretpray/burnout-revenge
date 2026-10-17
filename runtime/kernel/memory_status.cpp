// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See memory_status.h.
#include "memory_status.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>

#ifdef __APPLE__
#include <TargetConditionals.h>
#include <mach/mach.h>
#include <platform/code_signature.h>
#if TARGET_OS_IOS
#include <os/proc.h>
#endif
#endif

namespace memstatus
{
    namespace
    {
        constexpr const char* kExtendedVa = "com.apple.developer.kernel.extended-virtual-addressing";
        constexpr const char* kIncreasedMemory = "com.apple.developer.kernel.increased-memory-limit";
        // The mirrors' errors are kern_return_t values on Apple (Mach memory
        // entries, kernel/memory.cpp), errno elsewhere.
#ifdef __APPLE__
        constexpr bool kMachErrors = true;
#else
        constexpr bool kMachErrors = false;
#endif

        std::string ErrorText(const Status& s)
        {
            if (s.failure == Failure::AddressSpace)
                return std::format("reserving 4 GB failed: {} (errno {})", s.errorText.empty() ? "unknown error" : s.errorText, s.error);
            if (s.failure == Failure::PhysicalMemory)
                return std::format("mapping the 512 MB of physical memory failed: {} ({})",
                    s.errorText.empty() ? "unknown error" : s.errorText, s.error);
            return "";
        }

        // "entitlements in the signature: Extended Virtual Addressing yes,
        // Increased Memory Limit no", or why they aren't known.
        std::string EntitlementsText(const Status& s)
        {
            if (!s.signatureRead)
                return "entitlements: not read";
            return std::format("entitlements in the signature: Extended Virtual Addressing {}, Increased Memory Limit {}",
                FlagName(s.extendedVirtualAddressing), FlagName(s.increasedMemoryLimit));
        }

        std::string Facts(const Status& s)
        {
            std::string text = s.failure == Failure::None ? std::format("guest 4 GB at {:#x}", s.base) : ErrorText(s);
            if (s.ios)
            {
                text += "; " + EntitlementsText(s);
                if (s.addressSpaceTop)
                    text += std::format("; address space up to {:#x} ({} GB)", s.addressSpaceTop, s.addressSpaceTop >> 30);
                text += s.memoryLimitMB > 0 ? std::format("; memory limit {} MB", s.memoryLimitMB) : "; memory limit unknown";
            }
            return text;
        }

#if defined(__APPLE__) && TARGET_OS_IOS
        bool ReadVmInfo(task_vm_info_data_t& info, mach_msg_type_number_t& count)
        {
            count = TASK_VM_INFO_COUNT;
            return task_info(mach_task_self(), TASK_VM_INFO, task_info_t(&info), &count) == KERN_SUCCESS;
        }
#endif
    }

    const char* FlagName(Flag flag)
    {
        return flag == Flag::Yes ? "yes" : flag == Flag::No ? "no" : "unknown";
    }

    std::string LogLine(const Status& status)
    {
        return "[memory] " + Facts(status);
    }

    std::string SummaryItem(const Status& status)
    {
        return Facts(status);
    }

    Message StartupFailure(const Status& s)
    {
        Message m;
        if (s.failure == Failure::None)
            return m;
        m.details = "Details: " + Facts(s) + ".";
        if (!s.ios)
        {
            m.title = "BurnoutRevenge can't start";
            m.paragraphs.push_back(s.failure == Failure::AddressSpace
                ? "The system refused the 4 GB of address space the game needs to lay out the Xbox 360's memory."
                : "The system refused to map the Xbox 360's 512 MB of memory into the game's address space.");
            m.paragraphs.push_back("Close other programs and try again. On Linux, check that no address-space limit applies "
                                   "(ulimit -v must say unlimited).");
            return m;
        }
        m.title = "This copy of BurnoutRevenge can't start";
        if (s.failure == Failure::PhysicalMemory)
        {
            m.paragraphs.push_back("iOS refused to map the Xbox 360's 512 MB of memory into the game's address space.");
            m.paragraphs.push_back("Restart the device and try again. If it happens again, please report it with a "
                                   "screenshot of this page.");
            return m;
        }
        if (s.extendedVirtualAddressing == Flag::Yes)
        {
            m.paragraphs.push_back("iOS refused the 4 GB of address space the game needs to lay out the Xbox 360's memory, "
                                   "even though this copy has the Extended Virtual Addressing entitlement.");
            m.paragraphs.push_back("Restart the device and try again. If it happens again, please report it with a "
                                   "screenshot of this page.");
            return m;
        }
        // iOS gives an app room for 4 GB with Extended Virtual Addressing,
        // or with Increased Memory Limit on devices that support it (both
        // lift its address space past 63 GB); without either it has about
        // 5 GB in one piece above the system's shared region, and the 4 GB
        // fits there only if nothing took that space first. A free Apple Account can
        // never give Extended Virtual Addressing; some sideloading tools
        // keep Increased Memory Limit for it (scripts/ios/README.md).
        std::string why = "iOS refused the 4 GB of address space the game needs for the Xbox 360's memory layout. ";
        if (s.extendedVirtualAddressing == Flag::No && s.increasedMemoryLimit == Flag::No)
            why += "iOS gives an app that much room with the Extended Virtual Addressing or the Increased Memory Limit "
                   "entitlement, and this copy was signed with neither.";
        else if (s.extendedVirtualAddressing == Flag::No)
            why += "This copy was signed with Increased Memory Limit, which gives that room only on devices that support "
                   "it, but not with Extended Virtual Addressing.";
        else
            why += "It takes the Extended Virtual Addressing or the Increased Memory Limit entitlement, and iOS didn't "
                   "give this copy the room.";
        m.paragraphs.push_back(why);
        m.paragraphs.push_back("This build needs the Extended Virtual Addressing entitlement, which a paid Apple Developer "
                               "Program account can give, or Increased Memory Limit, which AltStore 2.2 or later keeps even "
                               "with a free Apple Account (SideStore 0.7 and some other tools drop it). Sideload "
                               "BurnoutRevenge again that way, or build it yourself with Xcode: see scripts/ios/README.md in "
                               "the source.");
        m.paragraphs.push_back("You can close BurnoutRevenge now: swipe it away in the app switcher.");
        return m;
    }

    std::string LowMemoryNotice(const Status& s)
    {
        if (!s.ios || s.failure != Failure::None)
            return "";
        bool smallSpace = s.addressSpaceTop != 0 && s.addressSpaceTop < kLargeAddressSpace;
        bool lowLimit = s.memoryLimitMB > 0 && s.memoryLimitMB < kPeakFootprintMB;
        if (smallSpace && lowLimit)
            return std::format("iOS gives BurnoutRevenge little room ({} GB of address space, {} MB of memory: no memory "
                               "entitlements): it may close the game in busy scenes", s.addressSpaceTop >> 30, s.memoryLimitMB);
        if (smallSpace)
            return std::format("iOS gives BurnoutRevenge only {} GB of address space (no memory entitlements): it may close "
                               "the game in busy scenes", s.addressSpaceTop >> 30);
        if (lowLimit && s.increasedMemoryLimit != Flag::Yes)
            return std::format("iOS lets BurnoutRevenge use only {} MB (no Increased Memory Limit entitlement): it may close "
                               "the game in busy scenes", s.memoryLimitMB);
        if (lowLimit)
            return std::format("iOS lets BurnoutRevenge use only {} MB on this device: it may close the game in busy scenes",
                s.memoryLimitMB);
        return "";
    }

    void Gather(Status& status)
    {
        if (status.failure == Failure::AddressSpace || (status.failure == Failure::PhysicalMemory && !kMachErrors))
            status.errorText = std::strerror(status.error);
#ifdef __APPLE__
        else if (status.failure == Failure::PhysicalMemory)
            status.errorText = mach_error_string(status.error);
#endif
#if defined(__APPLE__) && TARGET_OS_IOS
        status.ios = true;
#else
        // BURNOUT_TEST_MEMORY_IOS=1: the iOS wording on another system (with
        // BURNOUT_TEST_RESERVE_FAIL, the iOS page), the entitlements read from
        // this executable's own signature (a Mac build has none: "no").
        const char* test = std::getenv("BURNOUT_TEST_MEMORY_IOS");
        status.ios = test && test[0] == '1';
#endif
#ifdef __APPLE__
        if (!status.ios)
            return;
        if (std::optional<platform::codesign::Entitlements> e = platform::codesign::ReadOwn())
        {
            using platform::codesign::Lookup;
            using platform::codesign::Value;
            status.signatureRead = true;
            status.extendedVirtualAddressing = Lookup(*e, kExtendedVa) == Value::True ? Flag::Yes : Flag::No;
            status.increasedMemoryLimit = Lookup(*e, kIncreasedMemory) == Value::True ? Flag::Yes : Flag::No;
        }
#else
        (void)kExtendedVa;
        (void)kIncreasedMemory;
#endif
#if defined(__APPLE__) && TARGET_OS_IOS
        // The limit iOS kills the app at: what it has (its footprint) plus
        // what it may still take. The address space's top shows whether
        // Extended Virtual Addressing took effect.
        task_vm_info_data_t info{};
        mach_msg_type_number_t count = 0;
        size_t available = os_proc_available_memory();
        if (ReadVmInfo(info, count))
        {
            if (available > 0)
                status.memoryLimitMB = int64_t((uint64_t(info.phys_footprint) + available) >> 20);
            if (count >= TASK_VM_INFO_REV2_COUNT)
                status.addressSpaceTop = info.max_address;
        }
#endif
    }

    void Poll()
    {
#if defined(__APPLE__) && TARGET_OS_IOS
        using clock = std::chrono::steady_clock;
        static clock::time_point s_next;
        clock::time_point now = clock::now();
        if (now < s_next)
            return;
        s_next = now + std::chrono::seconds(60);
        task_vm_info_data_t info{};
        mach_msg_type_number_t count = 0;
        if (!ReadVmInfo(info, count))
            return;
        int64_t peak = count >= TASK_VM_INFO_REV3_COUNT ? info.ledger_phys_footprint_peak : 0;
        fprintf(stderr, "[memory] footprint %llu MB, peak %lld MB, %zu MB left before iOS's limit\n",
            (unsigned long long)(info.phys_footprint >> 20), (long long)(peak >> 20), os_proc_available_memory() >> 20);
#endif
    }
}
