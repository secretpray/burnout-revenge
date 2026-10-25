// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Catalogue arguments and settings persistence are checked without game data.
#include <user/localization.h>
#include <user/language_resources.h>
#include <user/settings.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

static std::vector<std::string> Arguments(const char* text)
{
    static const std::regex argument(R"(%(?:[-+#0]*[0-9.*]*)(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn%]|\{[^{}]*\})");
    std::string value(text); std::vector<std::string> result;
    for (auto it = std::sregex_iterator(value.begin(), value.end(), argument); it != std::sregex_iterator(); ++it)
        result.push_back(it->str());
    return result;
}
int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--crash")
    {
        assert(settings::Load());
        assert(settings::GetBool(settings::Id::CrashLaunchMeter) == bool(std::atoi(argv[2])));
        assert(settings::GetStoredValue(settings::Id::CrashLaunchMeter) == 1);
        return 0;
    }
    if (argc == 4 && std::string_view(argv[1]) == "--read")
    {
        assert(settings::Load());
        assert(settings::GetInt(settings::Id::Language) == std::atoi(argv[2]));
        assert(settings::GetStoredValue(settings::Id::Language) == std::atoi(argv[3]));
        assert(settings::GetMenuValue(settings::Id::Language) == std::atoi(argv[2]));
        assert((settings::EnvOverride(settings::Id::Language) != nullptr) == (std::getenv("BURNOUT_LANGUAGE") != nullptr));
        assert(settings::LastError().empty());
        return 0;
    }
    assert(argc == 2);
    std::set<std::string_view> seen;
    for (auto& message : locale::Messages())
    {
        assert(seen.emplace(message.text[0]).second);
        const auto signature = Arguments(message.text[0]);
        for (int language = 1; language <= int(locale::codes.size()); ++language)
        {
            const char* translated = locale::TextFor(locale::Language(language), message.text[0]);
            assert(translated && *translated);
            if (Arguments(translated) != signature)
            { fprintf(stderr, "Arguments differ: %s -> %s\n", message.text[0], translated); return 1; }
            // Stable hidden IDs must not depend on language.
            std::string_view english(message.text[0]), localized(translated);
            if (auto id = english.find("###"); id != english.npos)
                assert(localized.substr(localized.find("###")) == english.substr(id));
        }
    }
    for (auto& option : settings::Options())
        if (!option.hidden)
            for (int language = 2; language <= int(locale::codes.size()); ++language)
                if (!seen.contains(option.description))
                { fprintf(stderr, "Missing help: %s\n%s\n", option.label, option.description); return 1; }
    for (int language = 1; language <= int(locale::codes.size()); ++language)
    {
        locale::Select(language);
        assert(int(locale::Current()) == language);
        assert(locale::Format("{} connected", 3).find('3') != std::string::npos);
        assert(std::string_view(locale::Text("unknown translation key")) == "unknown translation key");
    }
    locale::Select(999); assert(locale::Current() == locale::Language::English);
    auto root = std::filesystem::path(argv[1]) / "localization-test";
    std::filesystem::remove_all(root); std::filesystem::create_directories(root);
    setenv("BR_DATA_DIR", root.c_str(), 1); unsetenv("BURNOUT_LANGUAGE");
    assert(settings::Load());
    assert(!settings::GetBool(settings::Id::CrashLaunchMeter));
    assert(settings::Set(settings::Id::CrashLaunchMeter, 1) == settings::SetResult::Applied);
    assert(settings::GetBool(settings::Id::CrashLaunchMeter));
    assert(!settings::RestartPending(settings::Id::CrashLaunchMeter));
    assert(settings::GetInt(settings::Id::Language) == 1);
    assert(settings::Set(settings::Id::Language, 2) == settings::SetResult::Deferred);
    assert(settings::GetInt(settings::Id::Language) == 1);
    assert(settings::GetMenuValue(settings::Id::Language) == 2);
    assert(settings::RestartPending(settings::Id::Language));
    assert(settings::Save());
    auto crashRelaunch = [&](const char* expected) {
        pid_t child = fork(); assert(child >= 0);
        if (child == 0) { execl(argv[0], argv[0], "--crash", expected, nullptr); _exit(127); }
        int status = 0; assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    };
    crashRelaunch("1");
    setenv("BURNOUT_CRASH_LAUNCH_METER", "0", 1); crashRelaunch("0");
    unsetenv("BURNOUT_CRASH_LAUNCH_METER"); crashRelaunch("1");
    auto relaunch = [&](const char* expected, const char* stored = "2") {
        pid_t child = fork(); assert(child >= 0);
        if (child == 0) { execl(argv[0], argv[0], "--read", expected, stored, nullptr); _exit(127); }
        int status = 0; assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    };
    relaunch("2");
    setenv("BURNOUT_LANGUAGE", "de", 1); relaunch("3");
    unsetenv("BURNOUT_LANGUAGE"); relaunch("2");
    const std::array<int, 9> nativeIds{1, 2, 3, 4, 5, 6, 10, 11, 12};
    for (int id = 1; id <= int(locale::codes.size()); ++id)
    {
        auto language = locale::Language(id);
        assert(locale::NativeLanguageId(language) == nativeIds[id - 1]);
        assert(locale::XboxLanguageFor(language) == (id <= 6 ? uint32_t(id) : 1));
    }
    for (int id = 7; id <= 9; ++id)
    {
        const std::string value = std::to_string(id);
        setenv("BURNOUT_LANGUAGE", locale::codes[id - 1], 1); relaunch(value.c_str());
        unsetenv("BURNOUT_LANGUAGE");
        assert(settings::Set(settings::Id::Language, id) == settings::SetResult::Deferred);
        assert(settings::Save()); relaunch(value.c_str(), value.c_str());
        // Restore the on-disk baseline for the next environment-override test.
        settings::Set(settings::Id::Language, 2); assert(settings::Save());
    }
    // Partial/corrupt language packs must never advertise a game language.
    auto data = root / "data"; std::filesystem::create_directories(data / "languages");
    { std::ofstream f(data / "languages" / "7387596df42acf56fc7a2befa2c127a070b5ed35140198993252f21bf67c3d97"); f << "corrupt"; }
    for (int language = 2; language <= int(locale::codes.size()); ++language)
    {
        assert(!locale::PrepareGame(locale::Language(language), root / "game", data));
        assert(locale::XboxLanguage() == 1 && locale::GameLanguageId() == 1);
        assert(!locale::Resource("language/strings/mainge.bin"));
        assert(!locale::Resource("../save/Player Files"));
    }
    auto result = locale::ImportResources(root / "missing.iso", data);
    assert(!result.Ok());
    std::filesystem::remove_all(root);
    puts("localization_test: catalogue arguments, coverage, language persistence and corrupt-pack fallback passed");
}
