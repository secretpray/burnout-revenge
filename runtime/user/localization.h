// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cfloat>
#include <atomic>
#include <array>
#include <format>
#include <string>
#include <string_view>
#include <span>
#include <cstdint>

namespace locale
{
    // Stable port language IDs. Native game and Xbox IDs are mapped explicitly.
    enum class Language : int { English = 1, Japanese, German, French, Spanish, Italian, Dutch, Swedish, Finnish };
    inline std::atomic<Language> current{Language::English};
    inline std::atomic<Language> gameLanguage{Language::English};
    inline constexpr std::array<const char*, 9> codes{"en", "ja", "de", "fr", "es", "it", "nl", "sv", "fi"};
    inline constexpr std::array<const char*, 9> names{"English", "日本語", "Deutsch", "Français", "Español", "Italiano", "Nederlands", "Svenska", "Suomi"};
    inline Language Checked(int value) { return value >= 1 && value <= int(codes.size()) ? Language(value) : Language::English; }
    inline Language Current() { return current.load(std::memory_order_relaxed); }
    inline void Select(int value) { current.store(Checked(value), std::memory_order_relaxed); }
    // Xbox/XDBF has no Dutch, Swedish or Finnish language IDs. Use English
    // system/achievement text rather than interpreting port IDs 7/8/9 as
    // Korean, Chinese or Portuguese. Burnout's USA suffix table at 0x82012FA0 has Du/Sw/Fi at 10/11/12.
    inline uint32_t XboxLanguageFor(Language language)
    {
        const int id = int(Checked(int(language)));
        return id <= 6 ? uint32_t(id) : 1;
    }
    inline int NativeLanguageId(Language language)
    {
        constexpr std::array<int, 9> ids{1, 2, 3, 4, 5, 6, 10, 11, 12};
        return ids[int(Checked(int(language))) - 1];
    }
    inline uint32_t XboxLanguage() { return XboxLanguageFor(gameLanguage.load(std::memory_order_relaxed)); }
    inline int GameLanguageId() { return NativeLanguageId(gameLanguage.load(std::memory_order_relaxed)); }
    // Unknown keys deliberately fall back to the English source string.
    // Catalogue pointers are stable; an unknown key returns the caller's pointer.
    const char* Text(const char* english);
    const char* TextFor(Language language, const char* english);
    template<class... Args> std::string Format(const char* english, Args&&... args)
    {
        return std::vformat(Text(english), std::make_format_args(args...));
    }
    struct Message { const char* text[codes.size()]; };
    std::span<const Message> Messages();
}
