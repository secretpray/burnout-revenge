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
    // Xbox 360 language IDs. File tokens remain independent of translated text.
    enum class Language : int { English = 1, Japanese, German, French, Spanish, Italian };
    inline std::atomic<Language> current{Language::English};
    inline std::atomic<Language> gameLanguage{Language::English};
    inline constexpr std::array<const char*, 6> codes{"en", "ja", "de", "fr", "es", "it"};
    inline constexpr std::array<const char*, 6> names{"English", "日本語", "Deutsch", "Français", "Español", "Italiano"};
    inline Language Checked(int value) { return value >= 1 && value <= 6 ? Language(value) : Language::English; }
    inline Language Current() { return current.load(std::memory_order_relaxed); }
    inline void Select(int value) { current.store(Checked(value), std::memory_order_relaxed); }
    inline uint32_t XboxLanguage() { return uint32_t(gameLanguage.load(std::memory_order_relaxed)); }
    inline int GameLanguageId() { int id = int(XboxLanguage()); return id; }
    // Unknown keys deliberately fall back to the English source string.
    // Catalogue pointers are stable; an unknown key returns the caller's pointer.
    const char* Text(const char* english);
    const char* TextFor(Language language, const char* english);
    template<class... Args> std::string Format(const char* english, Args&&... args)
    {
        return std::vformat(Text(english), std::make_format_args(args...));
    }
    struct Message { const char* text[6]; };
    std::span<const Message> Messages();
}
