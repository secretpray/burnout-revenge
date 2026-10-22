// Catalogue bounds, persistent profile, failure handling, duplicate writes,
// burst notifications, slot isolation and the guest enumeration wire format.
#include <cassert>
#include <cstring>
#include <fstream>
#include <thread>
#include "achievement_fixture.h"

int main(int argc, char** argv)
{
    assert(argc == 2);
    auto root = std::filesystem::path(argv[1]) / "achievements-test";
    std::filesystem::remove_all(root); std::filesystem::create_directories(root);
    auto database = achievementfixture::Database();
    auto definitions = achievements::ParseDatabase(database);
    assert(definitions.size()==2 && definitions[0].id==58 && definitions[1].name=="Second");
    for (size_t n=0;n<database.size();++n)
        assert(achievements::ParseDatabase(std::span(database).first(n)).empty());
    auto bad=database; achievementfixture::U32(bad,8,0xFFFFFFFF);
    assert(achievements::ParseDatabase(bad).empty());
    bad=database; achievementfixture::U32(bad,34,0xFFFFFFFF);
    assert(achievements::ParseDatabase(bad).empty());
    achievements::Store store;
    assert(store.Initialize(database,root/"achievements.txt"));
    assert(!store.Earn(4,58) && !store.Earn(0,99));
    std::array<std::thread,8> writers;
    for(auto& t:writers) t=std::thread([&] { for(int i=0;i<20;++i) assert(store.Earn(0,58)); });
    for(auto& t:writers) t.join();
    assert(store.PopNotice()->id==58 && !store.PopNotice());
    auto timestamp=store.Entries()[0].unlocked; assert(timestamp>0);
    for(unsigned u=0;u<4;++u) { assert(store.Earn(u,58)); assert(store.Earn(u,59)); }
    int notices=0; while(store.PopNotice()) ++notices; assert(notices==7);
    achievements::Store reload; assert(reload.Initialize(database,root/"achievements.txt"));
    assert(reload.Entries()[0].unlocked==timestamp && !reload.HasNotices());
    assert(reload.Earn(0,58) && !reload.HasNotices());
    auto entries=reload.Entries();
    std::vector<uint8_t> buffer(achievements::EnumerationSize(2,7)+16,0xCD);
    achievements::WriteDetails(std::span(buffer).first(buffer.size()-16),0x1000,7,2,definitions,entries);
    assert(achievements::Read32(buffer,0)==58 && achievements::Read32(buffer,36)==59);
    assert(achievements::Read32(buffer,4)==0x1048 && buffer[72]==0 && buffer[73]=='F');
    assert(achievements::Read32(buffer,32)==0x20009 && achievements::Read32(buffer,24)!=0);
    for(size_t i=buffer.size()-16;i<buffer.size();++i) assert(buffer[i]==0xCD);
    std::vector<uint8_t> basic(36); achievements::WriteDetails(basic,0x2000,0,1,std::span(definitions).first(1),std::span(entries).first(1));
    assert(achievements::Read32(basic,4)==0 && achievements::Read32(basic,20)==5);
    {std::ofstream out(root/"broken.txt");out<<"BRACH 1 454107DC\n0 58 invalid\n";}
    achievements::Store broken; assert(!broken.Initialize(database,root/"broken.txt")); assert(!broken.Earn(0,58));
    {std::ifstream in(root/"broken.txt");std::string text((std::istreambuf_iterator<char>(in)),{});assert(text.find("invalid")!=std::string::npos);}
    {std::ofstream out(root/"future.txt");out<<"BRACH 1 454107DC\n0 58 253402300800\n";}
    achievements::Store future; assert(!future.Initialize(database,root/"future.txt"));
    // A parent that is a file forces a durable-write failure, without root-permission assumptions.
    {std::ofstream out(root/"blocked");out<<"file";}
    achievements::Store unwritable; assert(unwritable.Initialize(database,root/"blocked"/"achievements.txt"));
    assert(!unwritable.Earn(0,58) && !unwritable.Entries()[0].unlocked && !unwritable.HasNotices());
    std::filesystem::remove(root/"blocked");
    assert(unwritable.Earn(0,58) && unwritable.Error().empty()); // retry after a transient write failure
    std::filesystem::remove_all(root);
    puts("achievements_test: catalogue bounds, persistence, concurrent duplicates, slots, notices, corruption and guest details passed");
}
