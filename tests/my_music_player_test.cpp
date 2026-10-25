// Burnout Revenge tests. GPL-3.0-or-later (see COPYING).
// Production scanner/selection: no metadata I/O on callers, cached tags,
// cancellation of a generation, removed media, and the track/entry limits.
#include <user/my_music.h>
#include <user/music_index.h>
#include <user/settings.h>
#include <platform/audio_file.h>
#include <atomic>
#include <cassert>
#include <fstream>
#include <thread>

using namespace std::chrono_literals;
namespace fs=std::filesystem;
static fs::path folder;
static std::atomic<unsigned> tagReads=0;
static std::atomic<bool> slow=false;
namespace user::my_music { fs::path Folder() { return folder; } }
namespace ui { void Toast(std::string,double) {} }
namespace platform::audio_file
{
    bool IsMusicFile(const fs::path& p) { return p.extension()==".wav"; }
    Tags ReadTags(const fs::path& p)
    {
        ++tagReads;
        if(slow) std::this_thread::sleep_for(250ms);
        return {p.stem().string(),"Artist",1};
    }
}

template<class F> void Until(F f)
{
    const auto deadline=std::chrono::steady_clock::now()+10s;
    while(!f()) {assert(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(1ms);}
}
int main(int argc,char**argv)
{
    const auto root=fs::path(argv[1])/"my_music_player_test";
    fs::remove_all(root);fs::create_directories(root/"a");fs::create_directories(root/"b");
    folder=root/"a";std::ofstream(folder/"one.wav")<<'x';slow=true;
    auto start=std::chrono::steady_clock::now();
    auto pick=user::my_music::Next();assert(pick.loading && pick.file.empty());
    Until([&]{return tagReads>0;});
    start=std::chrono::steady_clock::now();
    for(int i=0;i<1000;++i) assert(user::my_music::Next().loading);
    const auto readMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    assert(readMs<100);
    printf("my_music_player_test: 1000 cached picks during blocked metadata %.3f ms\n",readMs);
    // Folder switch while metadata is blocked. No previous-folder result leaks.
    folder=root/"b";std::ofstream(folder/"two.wav")<<'x';user::my_music::Library();
    assert(user::my_music::Next().file.empty());
    Until([&]{return user::my_music::Library()->ready;});slow=false;
    pick=user::my_music::Next();assert(pick.file.filename()=="two.wav" && pick.name=="Artist - two");
    auto previous=user::my_music::Library();auto reads=tagReads.load();
    user::my_music::Library(true);Until([&]{auto s=user::my_music::Library();return s!=previous && !s->scanning;});
    assert(tagReads==reads); // unchanged tags are cached on the worker
    reads=tagReads.load();
    std::ofstream(folder/"two.wav",std::ios::app)<<"changed";
    user::my_music::Library(true);
    Until([&]{return tagReads>reads;});
    fs::create_directories(root/"denied");
    fs::permissions(root/"denied",fs::perms::none);
    folder=root/"denied";user::my_music::Library();
    Until([&]{return user::my_music::Library()->ready;});
    assert(!user::my_music::Library()->error.empty());
    fs::permissions(folder,fs::perms::owner_all);
    folder=root/"b";user::my_music::Library();
    Until([&]{return user::my_music::Library()->ready;});
    fs::remove_all(folder);user::my_music::Library(true);
    Until([&]{auto s=user::my_music::Library();return s->ready && !s->error.empty();});
    assert(user::my_music::Next().file.empty());
    // Saturated libraries are bounded and labeled, never grow without limit.
    fs::create_directories(folder);
    for(int i=0;i<4100;++i) std::ofstream(folder/(std::to_string(i)+".wav"))<<'x';
    user::my_music::Library(true);
    Until([&]{return user::my_music::Library()->limited;});
    assert(user::my_music::Library()->tracks.size()==4096);
    user::my_music::StopLibrary();
    fs::create_directories(root/"baseline");
    for(int i=0;i<64;++i) std::ofstream(root/"baseline"/(std::to_string(i)+".wav"))<<'x';
    start=std::chrono::steady_clock::now();
    const auto baseline=user::my_music::Scan(root/"baseline",[](const fs::path& p){
        std::this_thread::sleep_for(3ms);return p.extension()==".wav";
    });
    const auto baselineMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    assert(baseline.size()==64);
    printf("my_music_player_test: old synchronous traversal of 64 slow entries %.3f ms; cached caller does no file I/O\n",baselineMs);
    fs::remove_all(root);
}
