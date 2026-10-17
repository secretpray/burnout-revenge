// video::ChooseGpu and GpuLogLine (runtime/video/gpu_choice.h): the Vulkan
// device the presenter draws with, and the log's line about it. Header only;
// built by tests/CMakeLists.txt (-DBR_TESTS=ON), or from the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/gpu_choice_test.cpp -o build/gpu_choice_test
// 1. The devices we ship on, as their logs name them (one GPU each): that GPU,
//    the loader's first, as up to v0.1.1 ("the only one").
// 2. Laptops with two GPUs: Intel + NVIDIA (PR #6's i9-13900H + RTX 3050, in
//    either order), AMD integrated + NVIDIA, AMD + AMD; an NVIDIA GPU alone; a
//    lone integrated GPU.
// 3. A software renderer (llvmpipe) beside a real GPU, in either order; Mesa's
//    Dozen after native drivers; two discrete GPUs: the one with more
//    device-local memory, the loader's first when equal.
// 4. A device that can't present is never chosen; one without an extension
//    the game needs comes after those with all.
// 5. BURNOUT_GPU: an index, part of a name in any case, a number that is part
//    of a name, a device that can't present, no match, empty.
// 6. The log line, word for word.
// 7. Against an independent ranking over 20,000 random lists, and the rule
//    before v0.1.2 (the loader's first that can present) wherever exactly one
//    device can present.
#include <video/gpu_choice.h>

#include <cstdio>
#include <random>
#include <tuple>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

using video::ChooseGpu;
using video::GpuCandidate;
using video::GpuChoice;
using video::kNoGpu;

namespace
{
    constexpr int kOther = 0, kIntegrated = 1, kDiscrete = 2, kVirtual = 3, kCpu = 4;
    constexpr uint64_t GB = 1ull << 30;

    GpuCandidate Dev(size_t index, int type, const char* name, uint64_t bytes, bool presents = true, const char* lacks = "",
        bool layered = false)
    {
        return { index, type, layered, name, bytes, presents, lacks };
    }

    // The rule up to v0.1.1: the loader's first device that can present.
    size_t OldRule(const std::vector<GpuCandidate>& list)
    {
        for (size_t i = 0; i < list.size(); i++)
            if (list[i].presents)
                return i;
        return kNoGpu;
    }

    std::string Picked(const std::vector<GpuCandidate>& list, const char* want = nullptr)
    {
        GpuChoice c = ChooseGpu(list, want);
        printf("   %-8s -> %s\n", want ? want : "-", video::GpuLogLine(list, c).c_str());
        return c.pick == kNoGpu ? std::string("(none)") : list[c.pick].name;
    }

    // An independent ranking (not GpuBefore): a key per device, smallest wins.
    size_t Reference(const std::vector<GpuCandidate>& list)
    {
        auto key = [](const GpuCandidate& c) {
            static const int order[] = { /*other*/ 3, /*integrated*/ 1, /*discrete*/ 0, /*virtual*/ 2, /*cpu*/ 4 };
            int kind = (c.type >= 0 && c.type <= 4) ? order[c.type] : 3;
            return std::make_tuple(c.lacks.empty() ? 0 : 1, c.layered ? 1 : 0, kind, ~c.localBytes, c.index);
        };
        size_t best = kNoGpu;
        for (size_t i = 0; i < list.size(); i++)
            if (list[i].presents && (best == kNoGpu || key(list[i]) < key(list[best])))
                best = i;
        return best;
    }
}

int main()
{
    printf("1. the devices we ship on (from their logs' [video] and GPU: lines)\n");
    {
        const std::vector<std::vector<GpuCandidate>> shipped = {
            { Dev(0, kIntegrated, "AMD Custom GPU 0932 (RADV VANGOGH)", 1 * GB) },          // Steam Deck OLED
            { Dev(0, kIntegrated, "AMD Custom GPU 0405 (RADV VANGOGH)", 1 * GB) },          // Steam Deck LCD
            { Dev(0, kDiscrete, "AMD Steam Machine (RADV NAVI33)", 8 * GB) },               // Steam Machine
            { Dev(0, kIntegrated, "Turnip Adreno (TM) 750", 15 * GB) },                     // Steam Frame
            { Dev(0, kIntegrated, "Apple M1 Pro", 10 * GB) },                               // MacBook Pro
            { Dev(0, kIntegrated, "Apple M4 Max", 27 * GB) },                               // Mac Studio
            { Dev(0, kIntegrated, "Apple Paravirtual device", 8 * GB) },                    // macOS 15 VM
            { Dev(0, kIntegrated, "Apple M2 GPU", 7 * GB) },                                // iPad Pro M2
            { Dev(0, kIntegrated, "Apple A17 Pro GPU", 5 * GB) },                           // iPhone 15 Pro Max
            { Dev(0, kDiscrete, "NVIDIA GeForce RTX 4070", 12 * GB) },                      // issue #2's desktops
            { Dev(0, kDiscrete, "NVIDIA GeForce GTX 1080 Ti", 11 * GB) },
            { Dev(0, kIntegrated, "Intel(R) Graphics (ARL)", 15 * GB) },                     // issue #2's Intel laptop
            { Dev(0, kIntegrated, "Intel(R) UHD Graphics", 3 * GB) },                       // the Windows test ThinkPad
        };
        for (const auto& one : shipped)
        {
            GpuChoice c = ChooseGpu(one, nullptr);
            CHECK(c.pick == 0 && c.pick == OldRule(one), "%s: picked %zu", one[0].name.c_str(), c.pick);
            CHECK(c.why == "the only one that can present to the window", "%s: %s", one[0].name.c_str(), c.why.c_str());
            std::string line = video::GpuLogLine(one, c);
            CHECK(line.find("BURNOUT_GPU") == std::string::npos && line.find("loader listed") == std::string::npos,
                "one device: no hint, no order note: %s", line.c_str());
            Picked(one);
        }
        // The Steam Machine's own log line, word for word (8.0 GB: RADV's VRAM heaps added up).
        CHECK(video::GpuLogLine(shipped[2], ChooseGpu(shipped[2], nullptr)) ==
                "Vulkan devices: 0 AMD Steam Machine (RADV NAVI33) (discrete, 8.0 GB); using 0: the only one that can present to the window",
            "%s", video::GpuLogLine(shipped[2], ChooseGpu(shipped[2], nullptr)).c_str());
    }

    printf("2. laptops with two GPUs\n");
    const std::vector<GpuCandidate> pr6 = { Dev(0, kIntegrated, "Intel(R) Iris(R) Xe Graphics (RPL-P)", 15 * GB),
        Dev(1, kDiscrete, "NVIDIA GeForce RTX 3050 Laptop GPU", 4 * GB) };
    {
        // The integrated GPU has more device-local memory (shared system memory): the kind decides first.
        GpuChoice c = ChooseGpu(pr6, nullptr);
        CHECK(c.pick == 1, "Intel first, NVIDIA second: picked %zu", c.pick);
        CHECK(c.why == "a discrete GPU before an integrated GPU", "why: %s", c.why.c_str());
        Picked(pr6);
        std::vector<GpuCandidate> nvFirst = { Dev(0, kDiscrete, "NVIDIA GeForce RTX 3050 Laptop GPU", 4 * GB),
            Dev(1, kIntegrated, "Intel(R) Iris(R) Xe Graphics (RPL-P)", 15 * GB) };
        CHECK(Picked(nvFirst) == "NVIDIA GeForce RTX 3050 Laptop GPU", "NVIDIA listed first (PRIME offload's order)");
        std::vector<GpuCandidate> amdNv = { Dev(0, kIntegrated, "AMD Radeon 780M (RADV PHOENIX)", 8 * GB),
            Dev(1, kDiscrete, "NVIDIA GeForce RTX 4060 Laptop GPU", 8 * GB) };
        CHECK(Picked(amdNv) == "NVIDIA GeForce RTX 4060 Laptop GPU", "AMD integrated + NVIDIA");
        std::vector<GpuCandidate> amdAmd = { Dev(0, kIntegrated, "AMD Radeon 680M (RADV REMBRANDT)", 2 * GB),
            Dev(1, kDiscrete, "AMD Radeon RX 6650M (RADV NAVI23)", 8 * GB) };
        CHECK(Picked(amdAmd) == "AMD Radeon RX 6650M (RADV NAVI23)", "AMD integrated + AMD discrete");
        std::vector<GpuCandidate> nvOnly = { Dev(0, kDiscrete, "NVIDIA GeForce RTX 3050 Laptop GPU", 4 * GB) };
        CHECK(Picked(nvOnly) == "NVIDIA GeForce RTX 3050 Laptop GPU", "NVIDIA only (PR #6's VK_DRIVER_FILES list)");
        std::vector<GpuCandidate> igpu = { Dev(0, kIntegrated, "Intel(R) Iris(R) Xe Graphics (RPL-P)", 15 * GB) };
        CHECK(Picked(igpu) == "Intel(R) Iris(R) Xe Graphics (RPL-P)", "a lone integrated GPU");
    }

    printf("3. software renderers, Dozen, two discrete GPUs\n");
    {
        std::vector<GpuCandidate> softFirst = { Dev(0, kCpu, "llvmpipe (LLVM 20.1.8, 256 bits)", 31 * GB),
            Dev(1, kDiscrete, "AMD Radeon RX 7600 (RADV NAVI33)", 8 * GB) };
        CHECK(Picked(softFirst) == "AMD Radeon RX 7600 (RADV NAVI33)", "llvmpipe listed first");
        CHECK(ChooseGpu(softFirst, nullptr).why == "a discrete GPU before a software renderer", "%s", ChooseGpu(softFirst, nullptr).why.c_str());
        std::vector<GpuCandidate> softLast = { Dev(0, kIntegrated, "AMD Custom GPU 0932 (RADV VANGOGH)", 1 * GB),
            Dev(1, kCpu, "llvmpipe (LLVM 20.1.8, 256 bits)", 15 * GB) };
        CHECK(Picked(softLast) == "AMD Custom GPU 0932 (RADV VANGOGH)", "an integrated GPU before llvmpipe, even with less memory");
        CHECK(OldRule(softLast) == ChooseGpu(softLast, nullptr).pick, "the same as before when the GPU is listed first");
        std::vector<GpuCandidate> otherVsSoft = { Dev(0, kCpu, "llvmpipe", 15 * GB), Dev(1, kOther, "Other", 1 * GB) };
        CHECK(Picked(otherVsSoft) == "Other", "software last of all native kinds");
        std::vector<GpuCandidate> virt = { Dev(0, kOther, "Other", 4 * GB), Dev(1, kVirtual, "Virtio-GPU Venus", 1 * GB) };
        CHECK(Picked(virt) == "Virtio-GPU Venus", "virtual before other");
        std::vector<GpuCandidate> dozen = { Dev(0, kDiscrete, "Microsoft Direct3D12 (NVIDIA GeForce RTX 4060)", 8 * GB, true, "", true),
            Dev(1, kCpu, "llvmpipe", 15 * GB), Dev(2, kIntegrated, "Intel(R) UHD Graphics", 2 * GB) };
        CHECK(Picked(dozen) == "Intel(R) UHD Graphics", "a native integrated GPU before Dozen's discrete one");
        dozen.pop_back();
        CHECK(Picked(dozen) == "llvmpipe", "Dozen after every native driver's (the windows branch's rule)");

        std::vector<GpuCandidate> two = { Dev(0, kDiscrete, "AMD Radeon RX 7600 (RADV NAVI33)", 8 * GB),
            Dev(1, kDiscrete, "NVIDIA GeForce RTX 4070 Ti SUPER", 16 * GB) };
        GpuChoice c = ChooseGpu(two, nullptr);
        CHECK(c.pick == 1, "two discrete: the one with more memory, picked %zu", c.pick);
        CHECK(c.why == "a discrete GPU, with more device-local memory than 0: 16.0 GB against 8.0 GB", "why: %s", c.why.c_str());
        Picked(two);
        two[1].localBytes = 8 * GB;
        c = ChooseGpu(two, nullptr);
        CHECK(c.pick == 0, "two discrete, equal memory: the loader's first, picked %zu", c.pick);
        CHECK(c.why == "a discrete GPU, with as much device-local memory as 1: the loader's order", "why: %s", c.why.c_str());
        // Memory never outranks the kind: an integrated GPU's shared 30 GB against a 4 GB card.
        std::vector<GpuCandidate> big = { Dev(0, kIntegrated, "AMD Radeon 8060S (RADV GFX1151)", 30 * GB),
            Dev(1, kDiscrete, "NVIDIA GeForce RTX 3050 Laptop GPU", 4 * GB) };
        CHECK(Picked(big) == "NVIDIA GeForce RTX 3050 Laptop GPU", "kind before memory");
    }

    printf("4. presenting, and the extensions the game needs\n");
    {
        std::vector<GpuCandidate> cant = { Dev(0, kDiscrete, "NVIDIA GeForce RTX 3050 Laptop GPU", 4 * GB, false),
            Dev(1, kIntegrated, "Intel(R) Iris(R) Xe Graphics (RPL-P)", 15 * GB) };
        CHECK(Picked(cant) == "Intel(R) Iris(R) Xe Graphics (RPL-P)", "a GPU that can't present is never chosen");
        CHECK(ChooseGpu(cant, nullptr).why == "the only one that can present to the window", "%s", ChooseGpu(cant, nullptr).why.c_str());
        std::vector<GpuCandidate> none = { Dev(0, kDiscrete, "A", 4 * GB, false), Dev(1, kIntegrated, "B", 1 * GB, false) };
        CHECK(ChooseGpu(none, nullptr).pick == kNoGpu, "none can present");
        CHECK(ChooseGpu(none, "1").pick == kNoGpu, "none can present, BURNOUT_GPU or not");
        CHECK(ChooseGpu({}, nullptr).pick == kNoGpu, "no devices");
        CHECK(video::GpuLogLine({}, ChooseGpu({}, nullptr)) == "Vulkan devices: none; none can present to the window", "%s",
            video::GpuLogLine({}, ChooseGpu({}, nullptr)).c_str());

        std::vector<GpuCandidate> old = { Dev(0, kIntegrated, "Intel(R) HD Graphics 4600 (HSW GT2)", 2 * GB),
            Dev(1, kDiscrete, "NVIDIA GeForce GTX 760M", 2 * GB, true, "VK_KHR_dynamic_rendering") };
        GpuChoice c = ChooseGpu(old, nullptr);
        CHECK(c.pick == 0, "a discrete GPU without an extension the game needs comes after: picked %zu", c.pick);
        CHECK(c.why == "1 NVIDIA GeForce GTX 760M lacks VK_KHR_dynamic_rendering, which the game needs", "why: %s", c.why.c_str());
        Picked(old);
        old[0].lacks = "VK_KHR_push_descriptor";
        CHECK(ChooseGpu(old, nullptr).pick == 1, "both lack one: the usual rules (it fails as it would have)");
        std::vector<GpuCandidate> lone = { Dev(0, kIntegrated, "Old", 1 * GB, true, "VK_KHR_dynamic_rendering") };
        CHECK(ChooseGpu(lone, nullptr).pick == 0, "a lone device without one: still it (vkCreateDevice says why, as before)");
    }

    printf("5. BURNOUT_GPU\n");
    {
        CHECK(Picked(pr6, "0") == "Intel(R) Iris(R) Xe Graphics (RPL-P)", "by index 0");
        CHECK(Picked(pr6, "1") == "NVIDIA GeForce RTX 3050 Laptop GPU", "by index 1");
        CHECK(Picked(pr6, "iris") == "Intel(R) Iris(R) Xe Graphics (RPL-P)", "part of a name, any case");
        CHECK(Picked(pr6, " Intel ") == "Intel(R) Iris(R) Xe Graphics (RPL-P)", "spaces around it");
        CHECK(Picked(pr6, "3050") == "NVIDIA GeForce RTX 3050 Laptop GPU", "a number that is no index: part of a name");
        CHECK(ChooseGpu(pr6, "intel").why == "BURNOUT_GPU=intel", "why: %s", ChooseGpu(pr6, "intel").why.c_str());
        GpuChoice c = ChooseGpu(pr6, "radeon");
        CHECK(c.pick == 1 && c.wantNote == "BURNOUT_GPU=radeon names none of them (an index from this list, or part of a name)",
            "no match: %zu, %s", c.pick, c.wantNote.c_str());
        c = ChooseGpu(pr6, "7");
        CHECK(c.pick == 1 && !c.wantNote.empty(), "an index beyond the list: the usual choice");
        c = ChooseGpu(pr6, "");
        CHECK(c.pick == 1 && c.wantNote.empty() && c.want.empty(), "empty: as unset");
        c = ChooseGpu(pr6, "  ");
        CHECK(c.pick == 1 && c.want.empty(), "blank: as unset");
        // The index is the loader's, not a position among the presenting.
        std::vector<GpuCandidate> gap = { Dev(0, kDiscrete, "A", 8 * GB, false), Dev(1, kIntegrated, "B", 1 * GB),
            Dev(2, kDiscrete, "C", 4 * GB) };
        CHECK(Picked(gap, "1") == "B", "index 1 is B");
        c = ChooseGpu(gap, "0");
        CHECK(c.pick == 2 && c.wantNote == "BURNOUT_GPU=0 names 0 A, which can't present to the window", "can't present: %zu %s", c.pick,
            c.wantNote.c_str());
        Picked(gap, "0");
        std::vector<GpuCandidate> nvs = { Dev(0, kDiscrete, "NVIDIA GeForce RTX 3060", 12 * GB, false),
            Dev(1, kDiscrete, "NVIDIA GeForce GTX 1650", 4 * GB), Dev(2, kDiscrete, "NVIDIA GeForce RTX 4070", 12 * GB),
            Dev(3, kIntegrated, "Intel(R) UHD Graphics 770", 15 * GB) };
        CHECK(Picked(nvs, "nvidia") == "NVIDIA GeForce RTX 4070", "several match: the best of them that can present");
        CHECK(Picked(nvs, "3060") == "NVIDIA GeForce RTX 4070", "matches only one that can't present: the usual choice");
        CHECK(ChooseGpu(nvs, "3060").wantNote == "BURNOUT_GPU=3060 names only devices that can't present to the window", "%s",
            ChooseGpu(nvs, "3060").wantNote.c_str());
    }

    printf("6. the log line\n");
    {
        std::string line = video::GpuLogLine(pr6, ChooseGpu(pr6, nullptr));
        CHECK(line == "Vulkan devices: 0 Intel(R) Iris(R) Xe Graphics (RPL-P) (integrated, 15.0 GB), 1 NVIDIA GeForce RTX 3050 Laptop GPU "
                      "(discrete, 4.0 GB); using 1: a discrete GPU before an integrated GPU (the loader listed 0 first); "
                      "BURNOUT_GPU=<index or part of a name> picks another", "%s", line.c_str());
        line = video::GpuLogLine(pr6, ChooseGpu(pr6, "0"));
        CHECK(line == "Vulkan devices: 0 Intel(R) Iris(R) Xe Graphics (RPL-P) (integrated, 15.0 GB), 1 NVIDIA GeForce RTX 3050 Laptop GPU "
                      "(discrete, 4.0 GB); using 0: BURNOUT_GPU=0 (the loader listed it first)", "%s", line.c_str());
        line = video::GpuLogLine(pr6, ChooseGpu(pr6, "amd"));
        CHECK(line.ends_with("; using 1: BURNOUT_GPU=amd names none of them (an index from this list, or part of a name), so the usual "
                             "choice: a discrete GPU before an integrated GPU (the loader listed 0 first)"), "%s", line.c_str());
        // The loader's first that can present, when that isn't its device 0.
        std::vector<GpuCandidate> gap = { Dev(0, kDiscrete, "A", 8 * GB, false), Dev(1, kIntegrated, "B", 1 * GB),
            Dev(2, kDiscrete, "C", 4 * GB) };
        line = video::GpuLogLine(gap, ChooseGpu(gap, "1"));
        CHECK(line == "Vulkan devices: 0 A (discrete, 8.0 GB, can't present to the window), 1 B (integrated, 1.0 GB), 2 C (discrete, 4.0 GB); "
                      "using 1: BURNOUT_GPU=1 (the loader listed it first)", "%s", line.c_str());
        line = video::GpuLogLine(gap, ChooseGpu(gap, nullptr));
        CHECK(line.ends_with("using 2: a discrete GPU before an integrated GPU (the loader listed 1 first); BURNOUT_GPU=<index or part of a name> picks another"),
            "%s", line.c_str());
        std::vector<GpuCandidate> marks = { Dev(0, kDiscrete, "X", 0, false), Dev(1, kIntegrated, "Y", 2 * GB, true, "VK_KHR_push_descriptor"),
            Dev(2, kDiscrete, "Z", 6 * GB, true, "", true) };
        line = video::GpuLogLine(marks, ChooseGpu(marks, nullptr));
        CHECK(line == "Vulkan devices: 0 X (discrete, can't present to the window), 1 Y (integrated, 2.0 GB, lacks VK_KHR_push_descriptor), "
                      "2 Z (discrete on Direct3D 12, 6.0 GB); using 2: 1 Y lacks VK_KHR_push_descriptor, which the game needs "
                      "(the loader listed 1 first); BURNOUT_GPU=<index or part of a name> picks another", "%s", line.c_str());
    }

    printf("7. against an independent ranking, and the old rule with one presenting device\n");
    {
        std::mt19937 rng(20261010);
        int checked = 0, single = 0, changed = 0;
        for (int round = 0; round < 20000; round++)
        {
            std::vector<GpuCandidate> list;
            size_t n = 1 + rng() % 5;
            for (size_t i = 0; i < n; i++)
            {
                // Few memory sizes, so ties happen; some lists skip indices.
                GpuCandidate c = Dev(i + (rng() % 4 == 0 ? 1 : 0) * 10, int(rng() % 6), "dev", (rng() % 3) * 4 * GB, rng() % 5 != 0,
                    rng() % 6 == 0 ? "VK_KHR_dynamic_rendering" : "", rng() % 7 == 0);
                c.name = std::format("dev{}", i);
                list.push_back(c);
            }
            GpuChoice c = ChooseGpu(list, nullptr);
            size_t ref = Reference(list);
            CHECK(c.pick == ref, "round %d: picked %zu, the reference %zu", round, c.pick, ref);
            size_t presenting = 0;
            for (const GpuCandidate& d : list)
                presenting += d.presents ? 1 : 0;
            if (presenting == 1)
            {
                single++;
                CHECK(c.pick == OldRule(list), "round %d: one can present: %zu, before v0.1.2 %zu", round, c.pick, OldRule(list));
            }
            changed += c.pick != OldRule(list) ? 1 : 0;
            checked++;
            if (g_failures > 20)
                break;
        }
        printf("   %d lists, %d with one presenting device (all the same as before), %d chose another than before\n", checked, single, changed);
    }

    printf(g_failures ? "%d failure(s)\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
