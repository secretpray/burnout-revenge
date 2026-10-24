// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Read retained HUD art from the player's installed USA Xbox 360 dictionary.
// No console assets are embedded in the port or imported from another disc.
#if defined(BR_GAME_BURNOUT)
#include "crash_launch_art.h"
#include <imgui_internal.h>
#include <game/crash_launch_texture.h>
#include <game/crash_launch_font.h>
#include <user/paths.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>

namespace ui::crash_art
{
    namespace
    {
        std::unique_ptr<ImTextureData> atlas;
        std::map<std::string, Sprite, std::less<>> sprites;
        bool tried{};
        std::unique_ptr<ImTextureData> fontTexture;
        game::crash_launch::texture::Font font;
        bool fontTried{};
        constexpr unsigned width = 1024, height = 832;
    }
    bool Load()
    {
        if (tried) return atlas != nullptr;
        tried = true;
        using namespace game::crash_launch::texture;
        std::ifstream stream(GetGamePath() / "data/Global.txd", std::ios::binary | std::ios::ate);
        const auto length = stream.tellg();
        if (length < 16 || length > 64 * 1024 * 1024) return false;
        std::vector<uint8_t> file(size_t(length), 0);
        stream.seekg(0);
        if (!stream.read(reinterpret_cast<char*>(file.data()), length)) return false;
        const auto span = std::span<const uint8_t>(file);
        const unsigned count = Big32(span, 8);
        if (Big32(span, 0) != 0xBCDEED81 || Big32(span, 4) != 0x543C0000 ||
            count > 4096 || size_t(count) * 16 > file.size() - 16) return false;
        std::vector<uint8_t> rgba(width * height * 4, 0);
        unsigned slot = 0;
        std::map<std::string, std::pair<ImVec2, ImVec2>> positions;
        for (unsigned i = 0; i < count; ++i)
        {
            const size_t offset = Big32(span, 16 + i * 16 + 8);
            if (offset > file.size() || file.size() - offset < 96) return false;
            const auto* name = reinterpret_cast<const char*>(file.data() + offset);
            const auto* end = static_cast<const char*>(std::memchr(name, 0, 32));
            if (!end) continue;
            const std::string_view key(name, end);
            if (key != "CrashBar" && key != "AgressivePos" && key != "BoostEarnFlame" &&
                !key.starts_with("BoostFireCore") && !key.starts_with("BoostFireEdge")) continue;
            Image image;
            if (!Decode(span, offset, image)) return false;
            const unsigned x = key == "CrashBar" || key == "AgressivePos" ? 0 : (slot % 8) * 128;
            const unsigned y = key == "CrashBar" ? 0 : key == "AgressivePos" ? 704 : 64 + (slot++ / 8) * 64;
            if ((key == "CrashBar" && (image.width != 512 || image.height != 64)) ||
                (key != "CrashBar" && (image.width > 128 || image.height > (key == "AgressivePos" ? 128u : 64u))) || y + image.height > height) return false;
            for (unsigned row = 0; row < image.height; ++row)
                std::memcpy(rgba.data() + ((y + row) * width + x) * 4,
                    image.rgba.data() + row * image.width * 4, image.width * 4);
            positions.emplace(key, std::pair{ImVec2(float(x) / width, float(y) / height),
                ImVec2(float(x + image.width) / width, float(y + image.height) / height)});
        }
        // Bar, outcome icon, blue flame, thirty core and forty-one edge frames.
        if (positions.size() != 74) return false;
        atlas = std::make_unique<ImTextureData>();
        atlas->Create(ImTextureFormat_RGBA32, width, height);
        atlas->UseColors = true;
        std::memcpy(atlas->GetPixels(), rgba.data(), rgba.size());
        ImGui::RegisterUserTexture(atlas.get());
        for (const auto& [name, uv] : positions) sprites.emplace(name, Sprite{atlas->GetTexRef(), uv.first, uv.second});
        fprintf(stderr, "[crash-hud] original_art=74 source=data/Global.txd atlas=1024x832\n");
        return true;
    }
    Sprite Get(std::string_view name)
    {
        const auto it = sprites.find(name);
        return it == sprites.end() ? Sprite{} : it->second;
    }
    bool LoadFont()
    {
        if (fontTried) return fontTexture != nullptr;
        fontTried = true;
        // Isolated retail comparison runs may supply the user's extracted
        // Xbox font. The installed 360 font has different distressed pixels.
        const char* reference = std::getenv("BURNOUT_CRASH_LAUNCH_REFERENCE_DIR");
        const auto path = reference ? std::filesystem::path(reference) / "Big.bin" : GetGamePath() / "language/fonts/Big.bin";
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        const auto length = stream.tellg();
        if (length < 544 || length > 8 * 1024 * 1024) return false;
        std::vector<uint8_t> file(size_t(length), 0);
        stream.seekg(0);
        if (!stream.read(reinterpret_cast<char*>(file.data()), length)) return false;
        if (!(reference ? game::crash_launch::texture::DecodeXboxFont(file, font) :
            game::crash_launch::texture::DecodeFont(file, font))) return false;
        fontTexture = std::make_unique<ImTextureData>();
        fontTexture->Create(ImTextureFormat_RGBA32, font.image.width, font.image.height);
        fontTexture->UseColors = true;
        std::memcpy(fontTexture->GetPixels(), font.image.rgba.data(), font.image.rgba.size());
        ImGui::RegisterUserTexture(fontTexture.get());
        font.image.rgba.clear();
        fprintf(stderr, "[crash-hud] original_font=Big platform=%s\n", reference ? "xbox-retail" : "360");
        return true;
    }
    float TextWidth(std::string_view text, float height)
    {
        float width = 0;
        for (unsigned char c : text) if (c < 128) width += font.glyphs[c].advance;
        return width * height * font.scaleX;
    }
    void Text(ImDrawList* draw, std::string_view text, ImVec2 at, float height, ImU32 color)
    {
        if (!fontTexture) return;
        const float sx = height * font.scaleX, sy = height * font.scaleY;
        for (unsigned char c : text)
        {
            if (c >= 128) continue;
            const auto& g = font.glyphs[c];
            if (g.height > 0)
                draw->AddImage(fontTexture->GetTexRef(), {at.x + g.x * sx, at.y + g.y * sy},
                    {at.x + (g.x + g.width) * sx, at.y + (g.y + g.height) * sy},
                    {g.u, g.v}, {g.u + g.width, g.v + g.height}, color);
            at.x += g.advance * sx;
        }
    }
    void Release()
    {
        if (fontTexture) ImGui::UnregisterUserTexture(fontTexture.get());
        fontTexture.reset();
        font = {};
        fontTried = false;
        if (atlas) ImGui::UnregisterUserTexture(atlas.get());
        sprites.clear();
        atlas.reset();
        tried = false;
    }
}
#endif
