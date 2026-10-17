// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

namespace xnotify
{
    // XNID_AREA, not MSG_AREA: XN_XMP_* (0x0A00xxxx) belong to area 5.
    inline bool MatchesArea(uint64_t mask, uint32_t id)
    {
        return (mask & (uint64_t(1) << ((id >> 25) & 0x3F))) != 0;
    }

    // Protected by gListenersMutex in XAM. Preserve every real event and its
    // order, including equal values (a song change can keep state=Playing).
    // Avoid vector's O(n) move on each unfiltered read; filtered reads still
    // search for and remove the first matching event.
    class Queue
    {
    public:
        using Event = std::pair<uint32_t, uint32_t>;
        void Push(uint32_t id, uint32_t value)
        {
            m_events.emplace_back(id, value);
            ++enqueued;
            highWater = std::max(highWater, m_events.size());
        }

        std::optional<Event> Pop(uint32_t filter = 0)
        {
            auto it = filter ? std::find_if(m_events.begin(), m_events.end(),
                [filter](const Event& e) { return e.first == filter; }) : m_events.begin();
            if (it == m_events.end()) return std::nullopt;
            Event event = *it;
            m_events.erase(it);
            ++dequeued;
            return event;
        }

        size_t Size() const { return m_events.size(); }
        size_t highWater = 0;
        uint64_t enqueued = 0, dequeued = 0;

    private:
        std::deque<Event> m_events;
    };
}
