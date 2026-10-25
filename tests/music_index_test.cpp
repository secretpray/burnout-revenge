// Burnout Revenge tests. GPL-3.0-or-later (see COPYING).
#include <user/music_index.h>
#include <user/my_music.h>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
namespace mm = user::my_music;
namespace fs = std::filesystem;

template<class F> void Until(F predicate)
{
    const auto end=std::chrono::steady_clock::now()+3s;
    while (!predicate()) { assert(std::chrono::steady_clock::now()<end); std::this_thread::sleep_for(1ms); }
}

int main(int argc,char**argv)
{
    const fs::path root=fs::path(argv[1])/"music_index_test";
    fs::remove_all(root); fs::create_directories(root/"a"); fs::create_directories(root/"b");
    std::ofstream(root/"a"/"one.mp3")<<'x'; std::ofstream(root/"b"/"two.mp3")<<'x';
    std::atomic<int> active=0,maximum=0,started=0,cancelled=0;
    mm::MusicIndex index([&](const fs::path& path,const mm::MusicIndex::Cancel& cancel){
        const int current=++active; maximum.store(std::max(maximum.load(),current)); ++started;
        for(int i=0;i<100 && !cancel();++i) std::this_thread::sleep_for(1ms);
        mm::LibrarySnapshot result;
        if(cancel()) ++cancelled;
        else
        {
            for(const auto& file:mm::Scan(path,[](const fs::path&p){return p.extension()==".mp3";}))
                result.tracks.push_back({file,file.stem().string()});
            if(!fs::exists(path)) result.error="missing";
        }
        --active; return result;
    },50ms);
    index.Request(root/"a"); Until([&]{return started.load()>0;});
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<10000;++i) { index.Request(root/"a"); auto s=index.Get(); assert(!s->ready); }
    const double readMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    assert(readMs<100); // whole batch must not wait for even one slow scan
    index.Request(root/"b");
    assert(!index.Get()->ready && index.Get()->tracks.empty());
    Until([&]{return index.Get()->ready;});
    assert(cancelled>0 && maximum==1);
    auto old=index.Get(); assert(old->folder==root/"b" && old->tracks.size()==1 && old->tracks[0].name=="two");
    std::ofstream(root/"b"/"three.mp3")<<'x'; index.Request(root/"b",true);
    Until([&]{return index.Get()->tracks.size()==2;});
    assert(old->tracks.size()==1); // immutable retained snapshot
    fs::remove(root/"b"/"two.mp3"); index.Request(root/"b",true);
    Until([&]{return index.Get()->tracks.size()==1 && index.Get()->tracks[0].name=="three";});
    fs::remove_all(root/"b"); index.Request(root/"b",true);
    Until([&]{return index.Get()->ready && !index.Get()->error.empty();});
    assert(index.Get()->tracks.empty()); // removed mount must not keep stale tracks
    index.Stop(); Until([&]{return active.load()==0;});

    // An OS read that cannot observe cancellation must not block Stop/destruction.
    struct Gate { std::mutex mutex; std::condition_variable cv; bool entered=false,release=false,finished=false; };
    auto gate=std::make_shared<Gate>();
    auto blocked=std::make_unique<mm::MusicIndex>([gate](const fs::path&,const mm::MusicIndex::Cancel&){
        std::unique_lock lock(gate->mutex); gate->entered=true; gate->cv.notify_all();
        gate->cv.wait(lock,[&]{return gate->release;}); gate->finished=true;
        return mm::LibrarySnapshot{};
    });
    blocked->Request(root);
    {std::unique_lock lock(gate->mutex); assert(gate->cv.wait_for(lock,1s,[&]{return gate->entered;}));}
    const auto stopStart=std::chrono::steady_clock::now(); blocked.reset();
    assert(std::chrono::steady_clock::now()-stopStart<50ms);
    {std::lock_guard lock(gate->mutex); gate->release=true; gate->cv.notify_all();}
    Until([&]{std::lock_guard lock(gate->mutex);return gate->finished;});
    fs::remove_all(root);
    printf("music_index_test: single worker, cancellation, immutable snapshots, edits/removal, blocked-I/O shutdown passed; 10000 requests/reads %.3f ms\n",readMs);
}
