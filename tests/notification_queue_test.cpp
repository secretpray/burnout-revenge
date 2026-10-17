// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <kernel/notification_queue.h>
#include <cassert>
#include <cstdio>

int main()
{
    constexpr uint32_t state = 0x0A000001, behavior = 0x0A000002;
    assert(xnotify::MatchesArea(1ull << 5, state));
    assert(!xnotify::MatchesArea(1, state));
    assert(xnotify::MatchesArea(1, 9));
    assert(xnotify::MatchesArea(1ull << 63, 63u << 25));
    xnotify::Queue q;
    assert(!q.Pop() && !q.Pop(state));
    q.Push(9, 1); q.Push(state, 1); q.Push(behavior, 0); q.Push(state, 1); q.Push(9, 0);
    assert(q.Size() == 5 && q.highWater == 5 && q.enqueued == 5);
    assert(!q.Pop(42) && q.Size() == 5 && q.dequeued == 0);
    assert(q.Pop(state) == xnotify::Queue::Event(state, 1));
    assert(q.Pop() == xnotify::Queue::Event(9, 1));
    assert(q.Pop(state) == xnotify::Queue::Event(state, 1)); // equal events are real
    assert(q.Pop() == xnotify::Queue::Event(behavior, 0));
    assert(q.Pop() == xnotify::Queue::Event(9, 0));
    assert(!q.Pop() && q.dequeued == 5 && q.highWater == 5);
    // A reader selecting another ID must leave the other events in order.
    for (uint32_t i = 0; i < 100000; ++i) q.Push(i % 2 ? state : behavior, i);
    for (uint32_t i = 1; i < 100; i += 2)
        assert(q.Pop(state) == xnotify::Queue::Event(state, i));
    for (uint32_t i = 0; i < 100000; ++i)
    {
        if (i < 100 && i % 2) continue;
        assert(q.Pop() == xnotify::Queue::Event(i % 2 ? state : behavior, i));
    }
    assert(!q.Pop() && q.enqueued == q.dequeued && q.highWater == 100000);
    puts("notification_queue_test: FIFO, filters, duplicate events, area masks and counters passed");
}
