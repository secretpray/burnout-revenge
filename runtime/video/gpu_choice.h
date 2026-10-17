// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Which Vulkan device the presenter draws with (video/presenter.cpp,
// ChooseDevice), and the log's one line about it. Up to v0.1.1 it was the
// loader's first that could present to the window. On a laptop with two GPUs
// (an Intel or AMD one in the processor beside an NVIDIA or AMD graphics
// card) the loader, and Mesa's device-select layer, usually list the
// integrated one first, so the game ran there (GitHub PR #6). Now, of the
// devices that can present to the window:
//   1. one with every device extension the game needs, before one without
//      (the game can't start on that);
//   2. a discrete GPU, then an integrated one, a virtual one, any other kind,
//      then a software renderer (llvmpipe); a layered one (Mesa's Dozen,
//      Vulkan on Direct3D 12, which lists the same GPUs again) after every
//      native driver's;
//   3. among equals, the one with the most device-local memory;
//   4. then the loader's order.
// A device that can't present to the window is never chosen. So where only
// one can present (a Steam Deck, a Steam Machine, a Steam Frame, a Mac, an
// iPhone or iPad: one GPU each), it is that one, as before.
// BURNOUT_GPU=<index or part of a name> picks one by hand: an index from the
// log's list, or any part of a name, in any case (several matching: the best
// of them by the rules above). A number that is no device's index is tried as
// part of a name ("3050").
//
// Header only and Vulkan-free (`type` holds VkPhysicalDeviceType's values):
// tests/gpu_choice_test.cpp. The windows branch's first version of this file
// had rules 2 and 4 (and BURNOUT_GPU); this one adds 1 and 3, and the log line.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

namespace video
{
    struct GpuCandidate
    {
        size_t index;                // in the loader's list (vkEnumeratePhysicalDevices)
        int type;                    // VkPhysicalDeviceType: 1 integrated, 2 discrete, 3 virtual, 4 CPU, else other
        bool layered = false;        // Mesa's Dozen: Vulkan on Direct3D 12
        std::string name;            // deviceName
        uint64_t localBytes = 0;     // its device-local memory heaps, added up
        bool presents = true;        // a graphics queue family can present to the window
        std::string lacks;           // a device extension the game needs that it doesn't have ("": none)
    };

    inline constexpr size_t kNoGpu = size_t(-1);

    inline const char* GpuKind(int type)
    {
        return type == 2 ? "discrete" : type == 1 ? "integrated" : type == 3 ? "virtual" : type == 4 ? "software" : "other";
    }

    // "a discrete GPU", "an integrated GPU on Direct3D 12".
    inline std::string GpuDescription(const GpuCandidate& c)
    {
        const char* text = c.type == 2 ? "a discrete GPU" : c.type == 1 ? "an integrated GPU" : c.type == 3 ? "a virtual GPU"
            : c.type == 4 ? "a software renderer" : "a device of another kind";
        return std::format("{}{}", text, c.layered ? " on Direct3D 12" : "");
    }

    // "7.6 GB" (binary gigabytes, as drivers size their heaps).
    inline std::string GpuMemory(uint64_t bytes)
    {
        return std::format("{:.1f} GB", double(bytes) / double(1ull << 30));
    }

    // Rule 2's order: lower comes first.
    inline int GpuKindRank(const GpuCandidate& c)
    {
        int kind = c.type == 2 ? 0 : c.type == 1 ? 1 : c.type == 3 ? 2 : c.type == 4 ? 4 : 3;
        return kind + (c.layered ? 5 : 0);
    }

    // Rules 1-4: whether `a` comes before `b`.
    inline bool GpuBefore(const GpuCandidate& a, const GpuCandidate& b)
    {
        if (a.lacks.empty() != b.lacks.empty())
            return a.lacks.empty();
        if (GpuKindRank(a) != GpuKindRank(b))
            return GpuKindRank(a) < GpuKindRank(b);
        if (a.localBytes != b.localBytes)
            return a.localBytes > b.localBytes;
        return a.index < b.index;
    }

    struct GpuChoice
    {
        size_t pick = kNoGpu;  // into the list; kNoGpu: none can present to the window
        std::string why;       // why that one, for the log
        std::string want;      // BURNOUT_GPU as given ("": not set)
        std::string wantNote;  // when BURNOUT_GPU was set but not used: why not ("": used, or not set)
    };

    namespace detail
    {
        inline std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }

        inline std::string Trim(const char* text)
        {
            std::string s(text ? text : "");
            size_t b = s.find_first_not_of(" \t"), e = s.find_last_not_of(" \t");
            return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
        }

        // The best of the presenting devices, by rules 1-4 (kNoGpu: none).
        inline size_t Best(const std::vector<GpuCandidate>& devices, size_t skip = kNoGpu)
        {
            size_t best = kNoGpu;
            for (size_t i = 0; i < devices.size(); i++)
                if (i != skip && devices[i].presents && (best == kNoGpu || GpuBefore(devices[i], devices[best])))
                    best = i;
            return best;
        }

        // Why `pick` came before `next` (the best of the rest), by the first
        // rule that tells them apart.
        inline std::string Why(const GpuCandidate& pick, const GpuCandidate& next)
        {
            if (pick.lacks.empty() != next.lacks.empty())
                return std::format("{} {} lacks {}, which the game needs", next.index, next.name, next.lacks);
            if (GpuKindRank(pick) != GpuKindRank(next))
                return std::format("{} before {}", GpuDescription(pick), GpuDescription(next));
            if (pick.localBytes != next.localBytes)
                return std::format("{}, with more device-local memory than {}: {} against {}", GpuDescription(pick), next.index,
                    GpuMemory(pick.localBytes), GpuMemory(next.localBytes));
            return std::format("{}, with as much device-local memory as {}: the loader's order", GpuDescription(pick), next.index);
        }
    }

    // `devices`: every device the loader lists, in its order. `want`:
    // BURNOUT_GPU, or null.
    inline GpuChoice ChooseGpu(const std::vector<GpuCandidate>& devices, const char* want)
    {
        GpuChoice choice;
        choice.pick = detail::Best(devices);
        if (choice.pick == kNoGpu)
            return choice;
        const size_t next = detail::Best(devices, choice.pick);
        choice.why = next == kNoGpu ? std::string("the only one that can present to the window")
                                    : detail::Why(devices[choice.pick], devices[next]);

        choice.want = detail::Trim(want);
        if (choice.want.empty())
            return choice;
        // An index from the log's list, else part of a name.
        size_t named = kNoGpu;
        char* end = nullptr;
        unsigned long long index = std::strtoull(choice.want.c_str(), &end, 10);
        if (end != choice.want.c_str() && *end == '\0' && std::isdigit((unsigned char)choice.want[0]))
            for (size_t i = 0; i < devices.size() && named == kNoGpu; i++)
                if (devices[i].index == index)
                    named = i;
        bool anyName = false;
        if (named == kNoGpu)
        {
            const std::string part = detail::Lower(choice.want);
            for (size_t i = 0; i < devices.size(); i++)
                if (detail::Lower(devices[i].name).find(part) != std::string::npos)
                {
                    anyName = true;
                    if (devices[i].presents && (named == kNoGpu || GpuBefore(devices[i], devices[named])))
                        named = i;
                }
        }
        if (named != kNoGpu && devices[named].presents)
        {
            choice.pick = named;
            choice.why = std::format("BURNOUT_GPU={}", choice.want);
            return choice;
        }
        choice.wantNote = named != kNoGpu
            ? std::format("BURNOUT_GPU={} names {} {}, which can't present to the window", choice.want, devices[named].index, devices[named].name)
            : anyName ? std::format("BURNOUT_GPU={} names only devices that can't present to the window", choice.want)
                      : std::format("BURNOUT_GPU={} names none of them (an index from this list, or part of a name)", choice.want);
        return choice;
    }

    // The log's line (after "[video] "): every device the loader listed, the
    // one used, and why.
    //   Vulkan devices: 0 Intel(R) Iris(R) Xe Graphics (integrated, 7.6 GB),
    //   1 NVIDIA GeForce RTX 3050 Laptop GPU (discrete, 4.0 GB); using 1: a
    //   discrete GPU before an integrated GPU (the loader listed 0 first);
    //   BURNOUT_GPU=<index or part of a name> picks another
    inline std::string GpuLogLine(const std::vector<GpuCandidate>& devices, const GpuChoice& choice)
    {
        std::string line = "Vulkan devices: ";
        if (devices.empty())
            line += "none";
        size_t presenting = 0, first = kNoGpu;
        for (size_t i = 0; i < devices.size(); i++)
        {
            const GpuCandidate& c = devices[i];
            line += std::format("{}{} {} ({}{}{}{}{})", i ? ", " : "", c.index, c.name, GpuKind(c.type),
                c.layered ? " on Direct3D 12" : "", c.localBytes ? ", " + GpuMemory(c.localBytes) : std::string(),
                c.presents ? "" : ", can't present to the window", c.lacks.empty() ? std::string() : ", lacks " + c.lacks);
            if (c.presents)
            {
                presenting++;
                if (first == kNoGpu)
                    first = i;
            }
        }
        if (choice.pick == kNoGpu)
            return line + "; none can present to the window";
        line += std::format("; using {}: {}", devices[choice.pick].index,
            choice.wantNote.empty() ? choice.why : choice.wantNote + ", so the usual choice: " + choice.why);
        if (presenting > 1)
        {
            line += choice.pick == first ? " (the loader listed it first)" : std::format(" (the loader listed {} first)", devices[first].index);
            if (choice.want.empty())
                line += "; BURNOUT_GPU=<index or part of a name> picks another";
        }
        return line;
    }
}
