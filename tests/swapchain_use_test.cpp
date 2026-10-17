// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// video::SwapchainUse (runtime/video/swapchain_use.h): the present waiter's
// thread uses a swapchain (its present waits, the driver's reports) only
// while it is the current one, and the main thread never destroys one under
// such a use. v0.1.4 and v0.1.5 read the driver's reports
// (vkGetPastPresentationTimingGOOGLE) after the wait's lock, so a fullscreen
// toggle on a 120 Hz Mac could destroy the swapchain under the read: an abort,
// "mutex lock failed". Fake swapchains carry a lifetime canary: a use that
// reaches a destroyed one, or another one than its present's, is counted.
// The optional argument is runtime/video/presenter.cpp: every waiter call that
// takes a swapchain must be inside a use, and the swapchain retired before it
// is destroyed (checked, then planted mutations of it that must fail).
//
// Build by hand: c++ -std=c++20 -O2 -I runtime tests/swapchain_use_test.cpp -o build/swapchain_use_test
#include <video/swapchain_use.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    // A swapchain as the waiter sees it. Never freed (a pool), so a use after
    // its destruction is seen, not undefined: `alive` is the canary.
    struct FakeSwapchain
    {
        std::atomic<bool> alive{ false };
        std::atomic<uint64_t> generation{ 0 };  // the one it was published as
        std::atomic<int> users{ 0 };
    };
    using Use = video::SwapchainUse<FakeSwapchain*>;

    // A use of `s` for a present of generation `generation`: true if it was
    // sound (alive, the present's own swapchain) throughout.
    bool Touch(FakeSwapchain* s, uint64_t generation, int spins = 0)
    {
        s->users++;
        bool ok = s->alive.load() && s->generation.load() == generation;
        for (volatile int i = 0; i < spins; i = i + 1)
            ;
        ok = ok && s->alive.load();
        s->users--;
        return ok;
    }

    void Basics()
    {
        printf("publish, use, retire:\n");
        FakeSwapchain a, b;
        Use use;
        int ran = 0;
        CHECK(!use.Use(0, [&](FakeSwapchain*) { ran++; }) && ran == 0, "nothing published: no use");
        const uint64_t ga = use.Publish(&a);
        CHECK(ga != 0, "a generation is never 0");
        FakeSwapchain* seen = nullptr;
        CHECK(use.Use(ga, [&](FakeSwapchain* s) { seen = s; }) && seen == &a, "the published one's generation: used, with it");
        CHECK(!use.Use(0, [&](FakeSwapchain*) { ran++; }) && ran == 0, "generation 0 (no swapchain): no use");
        CHECK(!use.Use(ga + 1, [&](FakeSwapchain*) { ran++; }) && ran == 0, "a generation not published yet: no use");
        use.Retire();
        CHECK(!use.Use(ga, [&](FakeSwapchain*) { ran++; }) && ran == 0, "retired: no use");
        const uint64_t gb = use.Publish(&b);
        CHECK(gb != ga, "each swapchain its own generation");
        CHECK(!use.Use(ga, [&](FakeSwapchain*) { ran++; }) && ran == 0, "an older swapchain's present: no use of the new one");
        CHECK(use.Use(gb, [&](FakeSwapchain* s) { seen = s; }) && seen == &b, "the new one: used");
        use.Retire();
        use.Retire();
        CHECK(!use.Use(gb, [&](FakeSwapchain*) { ran++; }) && ran == 0, "retired twice (Shutdown after a destroy): no use");
    }

    // MoltenVK's and Mesa's handles are pointers: a new swapchain can get a
    // destroyed one's. A present to the old one is not the new one's.
    void HandleReuse()
    {
        printf("a new swapchain with a destroyed one's handle:\n");
        FakeSwapchain a;
        Use use;
        const uint64_t g1 = use.Publish(&a);
        use.Retire();
        const uint64_t g2 = use.Publish(&a);  // the same handle again
        int ran = 0;
        CHECK(g1 != g2, "a new generation");
        CHECK(!use.Use(g1, [&](FakeSwapchain*) { ran++; }) && ran == 0, "the old present isn't waited for on the new swapchain");
        CHECK(use.Use(g2, [&](FakeSwapchain*) { ran++; }) && ran == 1, "the new one's present is");
    }

    // Retire() must not return while a use is under way (the main thread
    // destroys the swapchain right after it), and none may start after it.
    void RetireWaits()
    {
        printf("retire waits for a use under way:\n");
        FakeSwapchain a;
        a.alive = true;
        Use use;
        const uint64_t g = use.Publish(&a);
        a.generation = g;
        std::atomic<bool> inside{ false }, done{ false };
        std::thread user([&] {
            use.Use(g, [&](FakeSwapchain*) {
                inside = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                done = true;
            });
        });
        while (!inside)
            std::this_thread::yield();
        const auto t0 = std::chrono::steady_clock::now();
        use.Retire();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(done, "Retire returned while the use was still under way (after %.1f ms)", ms);
        user.join();
        int ran = 0;
        CHECK(!use.Use(g, [&](FakeSwapchain*) { ran++; }) && ran == 0, "no use after the retire");
    }

    struct StressCounts
    {
        uint64_t unsound = 0, destroyedInUse = 0, used = 0, refused = 0;
    };

    // The presenter's own shape: the main thread destroys the swapchain and
    // makes another, again and again (fullscreen toggles); the waiter waits
    // for presents and reads the driver's reports after each wait, for
    // presents of the current swapchain and of older ones still queued. The
    // new swapchain mostly gets the old one's handle (slot) again.
    // `v015Order`: the reports read after the wait's use, with the present's
    // handle, as v0.1.4 and v0.1.5 did.
    StressCounts RunStress(bool v015Order, int cycles)
    {
        FakeSwapchain pool[3];
        Use use;
        std::atomic<uint64_t> lastPresent{ 0 };  // the generation the latest present was for
        std::atomic<bool> stop{ false };
        std::atomic<uint64_t> unsound{ 0 }, used{ 0 }, refused{ 0 };
        uint64_t destroyedInUse = 0;
        std::thread waiter([&] {
            uint64_t seen[4] = {};
            for (uint64_t n = 0; !stop; n++)
            {
                // A present just queued, or (every other time) one queued a
                // few presents ago, perhaps to a swapchain since destroyed.
                seen[n % 4] = lastPresent.load();
                const uint64_t g = (n % 2) ? seen[(n + 1) % 4] : seen[n % 4];
                FakeSwapchain* handle = nullptr;
                const bool waited = use.Use(g, [&](FakeSwapchain* s) {  // the wait
                    handle = s;
                    if (!Touch(s, g, 200))
                        unsound++;
                });
                (waited ? used : refused)++;
                if (!v015Order)
                {
                    const bool read = use.Use(g, [&](FakeSwapchain* s) {  // the driver's reports
                        if (!Touch(s, g, 200))
                            unsound++;
                    });
                    (read ? used : refused)++;
                }
                else if (handle && !Touch(handle, g, 200))
                    unsound++;
                std::this_thread::yield();
            }
        });
        FakeSwapchain* current = nullptr;
        for (int i = 0; i < cycles; i++)
        {
            // DestroySwapchain: retire, then destroy.
            use.Retire();
            if (current)
            {
                destroyedInUse += current->users.load() != 0;
                current->alive = false;
            }
            // CreateSwapchain: make one and publish it; presents to it follow.
            size_t slot = current ? size_t(current - pool) : 0;
            if (i % 4 == 0)
                slot = (slot + 1) % 3;
            current = &pool[slot];
            current->alive = true;
            const uint64_t g = use.Publish(current);
            current->generation = g;
            for (int p = 0; p < 3; p++)
            {
                lastPresent = g;
                for (volatile int k = 0; k < 300; k = k + 1)
                    ;
            }
        }
        stop = true;
        waiter.join();
        return { unsound.load(), destroyedInUse, used.load(), refused.load() };
    }

    void Stress()
    {
        printf("the waiter against swapchains destroyed and made again:\n");
        constexpr int kCycles = 20000;
        const StressCounts c = RunStress(false, kCycles);
        CHECK(c.unsound == 0, "%llu uses reached a destroyed swapchain, or another than their present's", (unsigned long long)c.unsound);
        CHECK(c.destroyedInUse == 0, "%llu swapchains destroyed while in use", (unsigned long long)c.destroyedInUse);
        CHECK(c.used > 1000 && c.refused > 0, "the waiter both used swapchains (%llu) and was refused stale ones (%llu)",
            (unsigned long long)c.used, (unsigned long long)c.refused);
        printf("  %d recreations: %llu uses, %llu refused, %llu unsound\n", kCycles, (unsigned long long)c.used,
            (unsigned long long)c.refused, (unsigned long long)c.unsound);
        // The same with v0.1.5's order must reach destroyed swapchains (it
        // is a race: up to 5 tries), or the canary proves nothing.
        uint64_t old = 0;
        int tries = 0;
        while (old == 0 && tries < 5)
        {
            old += RunStress(true, kCycles).unsound;
            tries++;
        }
        CHECK(old > 0, "v0.1.5's order (the reports read after the wait's lock) never reached a destroyed swapchain in %d tries", tries);
        printf("  v0.1.5's order: %llu unsound uses (%d tr%s)\n", (unsigned long long)old, tries, tries == 1 ? "y" : "ies");
    }

    // The orders v0.1.5 had, step by step: Touch() catches each, so the
    // canary would have caught the bug.
    void OldOrders()
    {
        printf("v0.1.5's orders, step by step (each must be caught):\n");
        // 1. The wait checked the swapchain under the lock, and the reports
        //    were read after it: the swapchain destroyed in between.
        {
            FakeSwapchain a;
            Use use;
            const uint64_t g = use.Publish(&a);
            a.alive = true;
            a.generation = g;
            FakeSwapchain* waited = nullptr;
            use.Use(g, [&](FakeSwapchain* s) { waited = s; });  // the wait
            use.Retire();                                       // DestroySwapchain
            a.alive = false;
            CHECK(!Touch(waited, g), "the report read after the lock reaches the destroyed swapchain: not caught");
        }
        // 2. The present named its swapchain by handle: the waiter, holding a
        //    present of the destroyed one, found the new one at that handle.
        {
            FakeSwapchain a;
            Use use;
            const uint64_t g1 = use.Publish(&a);
            a.alive = true;
            a.generation = g1;
            FakeSwapchain* presentHandle = &a;
            use.Retire();
            a.alive = false;
            const uint64_t g2 = use.Publish(&a);
            a.alive = true;
            a.generation = g2;
            FakeSwapchain* current = &a;  // v0.1.5: if (t.swapchain == s_waitSwapchain) wait on it
            CHECK(presentHandle == current && !Touch(current, g1), "the old present waited for on the new swapchain: not caught");
        }
    }

    // presenter.cpp's calls that take the waiter's swapchain, and what the
    // main thread does around destroying one; the problems found.
    std::vector<std::string> SourceProblems(const std::string& src)
    {
        std::vector<std::string> problems;
        const std::string use = "s_swapchainUse.Use(";
        for (const char* call : { "s_waitForPresent(s_device", "s_pastTiming(s_device" })
        {
            int found = 0;
            for (size_t at = src.find(call); at != std::string::npos; at = src.find(call, at + 1))
            {
                found++;
                const size_t open = src.rfind(use, at);
                if (open == std::string::npos || src.find("});", open) < at)
                    problems.push_back(std::string(call) + " outside a use, at offset " + std::to_string(at));
            }
            if (!found)
                problems.push_back(std::string("no ") + call + " call found");
        }
        // A function's body: from its head to the brace at its own indent.
        auto body = [&](const char* head) {
            const size_t at = src.find(head);
            if (at == std::string::npos)
                return std::string();
            const size_t line = src.rfind('\n', at) + 1;
            const std::string end = "\n" + src.substr(line, at - line) + "}\n";
            return src.substr(at, src.find(end, at) - at);
        };
        const std::string destroy = body("void DestroySwapchain()");
        const size_t retire = destroy.find("s_swapchainUse.Retire();");
        const size_t destroyCall = destroy.find("vkDestroySwapchainKHR(");
        if (destroy.empty() || retire == std::string::npos || destroyCall == std::string::npos || retire > destroyCall)
            problems.push_back("DestroySwapchain doesn't retire the swapchain before destroying it");
        size_t destroys = 0;
        for (size_t at = src.find("vkDestroySwapchainKHR("); at != std::string::npos; at = src.find("vkDestroySwapchainKHR(", at + 1))
            destroys++;
        if (destroys != 1)
            problems.push_back("vkDestroySwapchainKHR called " + std::to_string(destroys) + " times (only DestroySwapchain may)");
        const std::string create = body("bool CreateSwapchain()");
        const size_t made = create.find("vkCreateSwapchainKHR(");
        const size_t published = create.find("s_swapchainGeneration = s_swapchainUse.Publish(s_swapchain);");
        if (made == std::string::npos || published == std::string::npos || published < made)
            problems.push_back("CreateSwapchain doesn't publish the swapchain it made");
        if (src.find("s_timings.push_back({ id, s_swapchainGeneration,") == std::string::npos)
            problems.push_back("a present's timing doesn't name its swapchain's generation");
        if (body("void Shutdown()").find("s_swapchainUse.Retire();") == std::string::npos)
            problems.push_back("Shutdown doesn't retire the swapchain");
        return problems;
    }

    std::string Replaced(std::string s, const std::string& from, const std::string& to)
    {
        const size_t at = s.find(from);
        if (at == std::string::npos)
            return {};
        return s.replace(at, from.size(), to);
    }

    void Presenter(const char* path)
    {
        printf("presenter.cpp:\n");
        std::ifstream in(path);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string src = ss.str();
        CHECK(!src.empty(), "can't read %s", path);
        if (src.empty())
            return;
        for (const std::string& p : SourceProblems(src))
            CHECK(false, "%s", p.c_str());
        // Planted mutations, each a way back to the bug: each must be found.
        struct Plant
        {
            const char* name;
            std::string from, to;
        };
        const Plant plants[] = {
            { "the reports read after the use (v0.1.5's order)", "            const int64_t hostToSteady = HostToSteadyNs();",
                "            s_pastTiming(s_device, VK_NULL_HANDLE, &count, nullptr);\n            const int64_t hostToSteady = HostToSteadyNs();" },
            { "a present wait outside a use", "                VkResult r = VK_ERROR_OUT_OF_DATE_KHR;  // its swapchain is gone\n",
                "                VkResult r = s_waitForPresent(s_device, VK_NULL_HANDLE, t.id, 0);\n" },
            { "no retire before the destroy", "                s_swapchainUse.Retire();  // the waiter is off it after this", "" },
            { "a second destroy elsewhere", "        s_swapchainUse.Retire();  // the present waiter is off the swapchain",
                "        vkDestroySwapchainKHR(s_device, s_swapchain, nullptr);  // the present waiter is off the swapchain" },
            { "never published", "s_swapchainGeneration = s_swapchainUse.Publish(s_swapchain);", "s_swapchainGeneration = 0;" },
            { "presents named by handle", "s_timings.push_back({ id, s_swapchainGeneration,", "s_timings.push_back({ id, s_swapchain," },
        };
        for (const Plant& p : plants)
        {
            const std::string mutated = Replaced(src, p.from, p.to);
            CHECK(!mutated.empty(), "plant \"%s\": its text isn't in presenter.cpp", p.name);
            if (!mutated.empty())
            {
                const bool caught = !SourceProblems(mutated).empty();
                CHECK(caught, "plant \"%s\" not caught", p.name);
                printf("  plant: %s: %s\n", p.name, caught ? "caught" : "NOT caught");
            }
        }
    }
}

int main(int argc, char** argv)
{
    Basics();
    HandleReuse();
    RetireWaits();
    Stress();
    OldOrders();
    if (argc > 1)
        Presenter(argv[1]);
    else
        printf("(presenter.cpp not given: its checks skipped)\n");
    printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
