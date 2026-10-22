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
    if (argc == 3 && std::string_view(argv[1]) == "--read")
    {
        assert(settings::Load());
        assert(settings::GetInt(settings::Id::Language) == std::atoi(argv[2]));
        assert(settings::GetStoredValue(settings::Id::Language) == 2);
        assert(settings::GetMenuValue(settings::Id::Language) == std::atoi(argv[2]));
        assert((settings::EnvOverride(settings::Id::Language) != nullptr) == (std::atoi(argv[2]) == 3));
        assert(settings::LastError().empty());
        return 0;
    }
    assert(argc == 2);
    std::set<std::string_view> seen;
    for (auto& message : locale::Messages())
    {
        assert(seen.emplace(message.text[0]).second);
        const auto signature = Arguments(message.text[0]);
        for (int language = 1; language <= 6; ++language)
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
            for (int language = 2; language <= 6; ++language)
                if (!seen.contains(option.description))
                { fprintf(stderr, "Missing help: %s\n%s\n", option.label, option.description); return 1; }
    for (int language = 1; language <= 6; ++language)
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
    assert(settings::GetInt(settings::Id::Language) == 1);
    assert(settings::Set(settings::Id::Language, 2) == settings::SetResult::Deferred);
    assert(settings::GetInt(settings::Id::Language) == 1);
    assert(settings::GetMenuValue(settings::Id::Language) == 2);
    assert(settings::RestartPending(settings::Id::Language));
    assert(settings::Save());
    auto relaunch = [&](const char* expected) {
        pid_t child = fork(); assert(child >= 0);
        if (child == 0) { execl(argv[0], argv[0], "--read", expected, nullptr); _exit(127); }
        int status = 0; assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    };
    relaunch("2");
    setenv("BURNOUT_LANGUAGE", "de", 1); relaunch("3");
    unsetenv("BURNOUT_LANGUAGE"); relaunch("2");
    // Partial/corrupt language packs must never advertise a game language.
    auto data = root / "data"; std::filesystem::create_directories(data / "languages");
    { std::ofstream f(data / "languages" / "7387596df42acf56fc7a2befa2c127a070b5ed35140198993252f21bf67c3d97"); f << "corrupt"; }
    for (int language = 2; language <= 6; ++language)
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
