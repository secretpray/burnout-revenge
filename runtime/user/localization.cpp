// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include "localization.h"
#include <unordered_map>

namespace locale
{
    namespace
    {
        constexpr Message messages[] = {
#include "localization_catalog.inc"
        };
        const auto& Index()
        {
            static const auto index = [] {
                std::unordered_map<std::string_view, const Message*> table;
                for (const auto& row : messages) table.emplace(row.text[0], &row);
                return table;
            }();
            return index;
        }
    }
    std::span<const Message> Messages() { return messages; }
    const char* TextFor(Language language, const char* english)
    {
        if (!english) return "";
        size_t index = size_t(Checked(int(language))) - 1;
        if (index == 0) return english;
        auto it = Index().find(english);
        return it == Index().end() || !*it->second->text[index] ? english : it->second->text[index];
    }
    const char* Text(const char* english) { return TextFor(Current(), english); }
}
