// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <game/rewind_gate.h>
#include <cassert>
#include <future>
int main()
{
    using namespace game::rewind;
    gate::BeginWrite();
    { gate::Boundary b;assert(!b); }
    // A guest file opened on one thread may close on another.
    auto close=std::async(std::launch::async,[] { gate::EndWrite(); });close.get();
    assert(gate::epoch.load()==2);
    std::future<void> writer;
    {
        gate::Boundary b;assert(b);
        writer=std::async(std::launch::async,[] { gate::BeginWrite(); });
        assert(writer.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
        assert(gate::epoch.load()==2);
    }
    writer.get();
    { gate::Boundary b;assert(!b); }
    gate::EndWrite();
    { gate::Boundary b;assert(b); }
    assert(gate::epoch.load()==4);
}
