// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See display_gamma.h.
#include "display_gamma.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>

namespace video::display_gamma
{
    namespace
    {
        // Command processor thread only.
        Decoder s_decoder;
        bool s_pwlLogged = false;

        // The last complete table, for the presenter.
        std::mutex s_mutex;
        Table s_published;
        std::atomic<uint64_t> s_generation{ 0 };

        // Since the process started (static initialisation), for the log.
        const auto s_launch = std::chrono::steady_clock::now();
        double Seconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - s_launch).count();
        }

        // "8->5.7 16->12.5 ...": what a few framebuffer values become, in
        // 8-bit levels (r=g=b), or per channel when they differ.
        std::string Describe(const Table& t)
        {
            std::string out;
            bool grey = true;
            for (const auto& e : t.entry)
                grey = grey && e[0] == e[1] && e[1] == e[2];
            char buf[64];
            for (uint32_t k : { 8u, 16u, 32u, 64u, 96u, 128u, 160u, 192u, 224u, 255u })
            {
                const auto& e = t.entry[k];
                if (grey)
                    snprintf(buf, sizeof(buf), " %u->%.1f", k, e[0] * 255.0 / 1023.0);
                else
                    snprintf(buf, sizeof(buf), " %u->%.1f/%.1f/%.1f", k, e[0] * 255.0 / 1023.0, e[1] * 255.0 / 1023.0,
                        e[2] * 255.0 / 1023.0);
                out += buf;
            }
            return std::string(grey ? "r=g=b:" : "r/g/b:") + out + (t == Table::Identity() ? " (identity)" : "");
        }

        void Publish(const Table& t)
        {
            {
                std::lock_guard lock(s_mutex);
                if (s_generation.load(std::memory_order_relaxed) != 0 && t == s_published)
                    return;  // the same ramp written again
                s_published = t;
            }
            uint64_t n = s_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
            fprintf(stderr, "[video] display gamma ramp #%llu from the game at %.1f s: %s\n", (unsigned long long)n, Seconds(),
                Describe(t).c_str());
        }
    }

    void OnRegisterWrite(uint32_t index, uint32_t value)
    {
        if (s_decoder.Write(index, value))
        {
            if (s_decoder.TableMode())
                Publish(s_decoder.table());
        }
        if (s_decoder.PwlWrites() && !s_pwlLogged)
        {
            s_pwlLogged = true;
            fprintf(stderr, "[video] display gamma: the game writes a PWL ramp (DC_LUT_PWL_DATA); only the 256-entry table is applied\n");
        }
    }

    uint64_t Generation()
    {
        return s_generation.load(std::memory_order_acquire);
    }

    uint64_t Latest(Table& out)
    {
        std::lock_guard lock(s_mutex);
        out = s_published;
        return s_generation.load(std::memory_order_relaxed);
    }
}
