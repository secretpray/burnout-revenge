#include <user/localization.h>
#include <user/language_resources.h>
#include <user/paths.h>
#include <future>
// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The settings menu, built from settings::Options(): one page per category,
// one row per option. Made for a controller first: the D-pad picks a row,
// left/right changes it, A toggles, LB/RB turn pages, B closes. The mouse
// works too (a click selects a row, its arrows change it, the tabs turn
// pages), and touch as the mouse (on iOS a drag scrolls the rows: ui.cpp).
// Advanced also has Save Bug Report (report/bug_report.cpp), Check for
// Updates (update_dialog.cpp), the build's identity (version and commit) and
// the legal notice. The footer's last button is Quit Game, which asks first
// (quit_game.h; not on iPhone or iPad).
#include <stdafx.h>
#include "ui.h"
#include "quit_game.h"
#include "achievements.h"
#include "burnout_setup.h"  // the game's look: its faces, hazard dashes, button glyphs
#include "menu_sounds.h"    // and its clicks
#include "save_backups.h"

#include <report/report.h>
#include <user/settings.h>
#include <hid/key_map.h>     // the Controls page: one row per control, two keys each
#include <hid/wheel.h>       // the Wheel page: the device, its pedals and what they read
#include <user/key_names.h>  // a key's name in the file, when SDL has none
#include <video/frame_rate.h>
#include <video/presenter.h>
#include <game/title.h>
#include <game/burnout_addons.h>
#include <platform/audio_file.h>
#include <user/my_music.h>
#include <user/music_index.h>
#include <user/online.h>
#include <user/online_identity.h>
#include <game/rewind_ring.h>
#include <game/madden_unlock.h>
#include <hid/input.h>
#include <hid/rewind_shortcuts.h>
#include <kernel/xam.h>
#include <apu/audio.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_misc.h>  // SDL_OpenURL: Music Folder in Finder
#include <atomic>
#include <mutex>
#include <memory>
#include <optional>
#include <thread>

#include <imgui.h>
#include <imgui_internal.h>  // ClosePopupToLevel: a dialog left open when the menu closed
#include <SDL3/SDL_keyboard.h>  // a key row's label on this layout

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

namespace ui
{
    namespace
    {
        using settings::Category;
        using settings::Id;

        Category s_category = Category::Display;
        bool s_tabPending = false;   // s_category was changed by LB/RB; the tab bar catches up next frame
        bool s_focusFirstRow = true; // put the controller's cursor on the page's first row
        Id s_described = Id::Count;  // the row the help line describes
        bool s_describedBugReport = false;  // ...or Save Bug Report's
        bool s_describedUpdate = false;     // ...or Check for Updates'
        bool s_opened = true;        // just opened: take focus
        bool s_describedAddons = false;
        bool s_describedAbout = false;
        bool s_describedSaveBackups = false;
        // The Online page's own rows: the two text options and Check Server.
        bool s_describedGamertag = false;
        bool s_describedServerAddress = false;
        bool s_describedCheck = false;
        bool s_openText = false;  // open the text dialog next frame
        settings::TextId s_textEditing = settings::TextId::Gamertag;
        char s_textBuffer[128] = {};
        // Check Server runs on a thread of its own (it waits on the network).
        std::mutex s_checkMutex;
        std::atomic<bool> s_checking{ false };
        std::optional<online::CheckResult> s_checkResult;
        std::string s_checkedServer;  // the server the result is about
        bool s_describedMusicFolder = false;
        bool s_describedMadden = false;
        bool s_openAddons = false;
        bool s_rewindHelp=false,s_chordHelp=false,s_chordRecording=false,s_chordArmed=false;
        uint16_t s_chordPeak=0;
        Id s_chordSetting=Id::Count;
        std::string s_chordMessage;
        constexpr hid::keymap::Entry kRewindKeys[]={
            {hid::keymap::Control::Count,Id::KeyRewind10,Id::KeyRewind10_2,hid::keymap::Role::Button},
            {hid::keymap::Control::Count,Id::KeyRewind20,Id::KeyRewind20_2,hid::keymap::Role::Button},
            {hid::keymap::Control::Count,Id::KeyRewind30,Id::KeyRewind30_2,hid::keymap::Role::Button}};
        const hid::keymap::Entry& KeyEntry(Id id)
        {
            if (id>=Id::KeyRewind10 && id<=Id::KeyRewind30_2)
                return kRewindKeys[(unsigned(id)-unsigned(Id::KeyRewind10))/2];
            return hid::keymap::EntryOf(id);
        }
        std::mutex s_addonPickerMutex;
        bool s_addonPickerOpen = false;
        std::optional<std::string> s_addonPicked;
        std::string s_addonMessage;

        bool ActionRow(const char* id, const char* label, const char* value, float valueWidth, bool& described, bool enabled);
        std::mutex s_languageMutex;
        std::optional<std::string> s_languagePick;
        bool s_languagePicker = false;
        std::future<install::Result> s_languageImport;
        std::string s_languageMessage;
        bool s_describedLanguages = false;
        void LanguageDiscPicked(void*, const char* const* paths, int)
        {
            std::lock_guard lock(s_languageMutex);
            s_languagePicker = false;
            if (paths && paths[0]) s_languagePick = paths[0];
            else if (!paths) s_languageMessage = "The file picker couldn't open. Try again.";
        }
        void LanguageResourcesRow(float width)
        {
            std::optional<std::string> picked;
            bool picking;
            { std::lock_guard lock(s_languageMutex); picked.swap(s_languagePick); picking = s_languagePicker; }
            if (picked && !s_languageImport.valid())
                s_languageImport = std::async(std::launch::async, [path = *picked, data = GetUserPath()] { return locale::ImportResources(path, data); });
            if (s_languageImport.valid() && s_languageImport.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto result = s_languageImport.get();
                std::lock_guard lock(s_languageMutex);
                s_languageMessage = result.message;
                Toast(locale::Text(result.message.c_str()), 8.0);
            }
            const bool busy = picking || s_languageImport.valid();
            ImGui::BeginDisabled(busy);
            bool selected = ActionRow("language-resources", "Import Languages from Disc...", busy ? "Importing..." : "Choose ISO", width, s_describedLanguages, true);
            ImGui::EndDisabled();
            if (selected)
            {
                if (const char* test = std::getenv("BURNOUT_TEST_PICK_LANGUAGE_ISO"); test && *test)
                { std::lock_guard lock(s_languageMutex); s_languagePick = test; }
                else
                {
                    { std::lock_guard lock(s_languageMutex); s_languagePicker = true; }
                    static constexpr SDL_DialogFileFilter filters[] = {{"Xbox 360 disc image", "iso"}};
                    SDL_ShowOpenFileDialog(LanguageDiscPicked, nullptr, GetWindow(), filters, 1, nullptr, false);
                }
            }
        }

        void AddonFolderPicked(void*, const char* const* files, int)
        {
            std::lock_guard lock(s_addonPickerMutex);
            s_addonPickerOpen = false;
            if (files && files[0]) s_addonPicked = files[0];
            else if (!files) s_addonMessage = "The folder picker couldn't open. Try again.";
        }

        bool DrawAddons()
        {
            if (!ImGui::IsPopupOpen("burnout-addons")) return false;
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(std::min(viewport->WorkSize.x - 40, 680.0f * Scale()), 0), ImGuiCond_Always);
            burnout::PushDialogStyle();
            const bool shown = ImGui::BeginPopupModal("burnout-addons", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
            burnout::PopDialogStyle();
            if (!shown) return true;
            burnout::DialogFrame();
            ImGui::PushFont(burnout::DisplayFont(), 34.0f);
            ImGui::TextUnformatted(locale::Text("ADD-ONS"));
            ImGui::PopFont();
            ImGui::TextWrapped(locale::Text("Connected this session: %zu"), game::BurnoutAddons().size());
            for (const auto& addon : game::BurnoutAddons()) ImGui::BulletText("%s", addon.name.c_str());
            if (game::BurnoutAddons().empty()) ImGui::TextWrapped("%s", locale::Text("No car packs connected yet."));
            ImGui::Spacing();
            ImGui::TextWrapped("%s", locale::Text("Auto-import Add-ons searches XBOX360/454107DC/00000002 on mounted drives at launch. You can also choose a folder below. Newly imported packs connect after you restart the game."));
            ImGui::TextWrapped(locale::Text("Installed folder: %s"), game::BurnoutAddonFolder().string().c_str());
            bool picking;
            std::optional<std::string> picked;
            std::string message;
            {
                std::lock_guard lock(s_addonPickerMutex);
                picking = s_addonPickerOpen;
                picked.swap(s_addonPicked);
                message = s_addonMessage;
            }
            if (picked)
            {
                message = game::ImportBurnoutAddons(*picked);
                std::lock_guard lock(s_addonPickerMutex);
                s_addonMessage = message;
            }
            if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
            ImGui::Spacing();
            const float width = ImGui::GetContentRegionAvail().x;
            ImGui::BeginDisabled(picking);
            bool choose = burnout::DialogOption("Import from Folder...", width);
            ImGui::EndDisabled();
            if (choose)
            {
                { std::lock_guard lock(s_addonPickerMutex); s_addonPickerOpen = true; }
                SDL_ShowOpenFolderDialog(AddonFolderPicked, nullptr, GetWindow(), nullptr, false);
            }
            if (burnout::DialogOption("Close", width) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                ImGui::CloseCurrentPopup();
            ImGui::Dummy(ImVec2(0, 6 * Scale()));
            burnout::Hints({ { "Select", "A" }, { "Back", "B" } });
            burnout::EndDialog();
            return true;
        }
        // The Controls page (KeyRow). A row's two keys are cells; the chosen
        // cell is the focused row's, and a wait is for one option of one
        // row (SettingsMenuTakesKey takes the press before ImGui sees it).
        Id s_waiting = Id::Count;    // the Key option a row waits for; Count: no wait
        SDL_Scancode s_refused = SDL_SCANCODE_UNKNOWN;  // a press the wait wouldn't take; the help line says so
        SDL_Scancode s_takenKey = SDL_SCANCODE_UNKNOWN;  // the key a wait took or refused, until its release
        Id s_slotRow = Id::Count;    // the row whose chosen cell s_slot is
        int s_slot = 0;              // 0: the row's first key, 1: its second
        constexpr const char* kPressAKey = "PRESS A KEY";
        constexpr const char* kNoKey = "NONE";
        // The Wheel page's Calibrate row (CalibrateRow): which step is
        // running, or none. Each one waits for A, so one long pedal press
        // can't run through the lot (hid/wheel.h).
        bool s_calibrating = false;
        hid::wheel::Step s_calStep = hid::wheel::Step::SteerLeft;
        bool s_describedCal = false;  // the help line describes that row

        void EndWait()
        {
            s_waiting = Id::Count;
            s_refused = SDL_SCANCODE_UNKNOWN;
        }

        constexpr int kCategories = int(Category::Count);

#if BR_QUIT_GAME
        constexpr const char* kQuitPopup = "quit";
        quit::Prompt s_quit;
#endif

        bool Pressed(ImGuiKey key)
        {
            return ImGui::IsKeyPressed(key, true);
        }

        int PageDelta()
        {
            if (s_chordRecording) return 0;
            int delta = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false))
                delta--;
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_E, false))
                delta++;
            return delta;
        }

        // iPhone and iPad: the Controls page only while a keyboard is
        // connected (nothing to bind without one). The Wheel page, on every
        // device, only while a wheel is plugged in: nothing on it means
        // anything otherwise. Switching the wheel off doesn't take the page
        // away with it (hid/wheel.h's Found against Playing), or the row
        // that switched it off couldn't switch it back on.
        bool PageShown(Category c)
        {
            if (c == Category::Wheel)
                return hid::wheel::Found();
#if defined(__APPLE__) && TARGET_OS_IOS
            if (c == Category::Online)
                return false;  // not on iOS (no lobby of its own there)
#endif
#if defined(__APPLE__) && TARGET_OS_IOS
            if (c == Category::Controls)
                return SDL_HasKeyboard();
#endif
            return true;
        }

        // The game's menus set their labels and values in capitals.
        std::string Upper(const char* text)
        {
            text = locale::Text(text);
            std::string upper(text);
            for (char& c : upper)
                if (locale::Current() == locale::Language::English && c >= 'a' && c <= 'z')
                    c = char(c - 'a' + 'A');
            return upper;
        }

        // A key's name for a cell: on this keyboard's layout ("Z" on a
        // French Mac for the position the file calls "w") when that is
        // Latin, else SDL's name of the position ("W" for that key under a
        // Cyrillic layout, which would show U+0426, and keep showing it after
        // a switch back: the menu reads the layout, not the key), else the
        // file's. "none" for no key. Main thread.
        std::string KeyLabel(double stored)
        {
            if (stored == double(settings::keynames::kNone))
                return locale::Text(kNoKey);
            if (!(stored >= 1 && stored < double(SDL_SCANCODE_COUNT)))
                return "?";
            SDL_Scancode sc = SDL_Scancode(int(stored));
            const char* name = SDL_GetKeyName(SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false));
            if (name && std::any_of(name, name + strlen(name), [](char c) { return (unsigned char)c >= 0x80; }))
                name = nullptr;  // not Latin: the position's name instead
            if (!name || !name[0])
                name = SDL_GetScancodeName(sc);
            if (!name || !name[0])
                name = settings::keynames::Token(uint16_t(stored));
            return name && name[0] ? Upper(name) : "?";
        }

        // A key the wait refused, for the help line.
        std::string RefusedName(SDL_Scancode sc)
        {
            std::string name = KeyLabel(double(sc));
            return name == "?" || name == kNoKey ? "That key" : name;
        }

        // The other controls that have any of this row's keys ("D-pad
        // Left"), in page order, each once; "" when none.
        std::string SharedWith(const hid::keymap::Entry& e)
        {
            const double mine[2] = { settings::GetStoredValue(e.id), settings::GetStoredValue(e.id2) };
            std::string text;
            for (const auto& option : settings::Options())
            {
                if (option.type!=settings::Type::Key) continue;
                const auto& other=KeyEntry(option.id);
                if (option.id!=other.id || other.id==e.id)
                    continue;
                const double theirs[2] = { settings::GetStoredValue(other.id), settings::GetStoredValue(other.id2) };
                bool shared = false;
                for (double m : mine)
                    for (double th : theirs)
                        shared |= m != double(settings::keynames::kNone) && m == th;
                if (shared)
                    text += (text.empty() ? "" : ", ") + std::string(settings::Info(other.id).label);
            }
            return text;
        }

        // A key cell is as wide as the widest name among the 48 keys, "Press
        // a key" or "none"; a row has two and a gap.
        float KeyCellWidth()
        {
            float width = std::max(ImGui::CalcTextSize(locale::Text(kPressAKey)).x, ImGui::CalcTextSize(locale::Text(kNoKey)).x);
            for (const auto& o : settings::Options())
                if (o.type == settings::Type::Key)
                    width = std::max(width, ImGui::CalcTextSize(KeyLabel(settings::GetStoredValue(o.id)).c_str()).x);
            return width;
        }

        float KeyCellGap()
        {
            return kSpaceM * Scale();
        }

        // Widest value this option can show, for the value column.
        float ValueWidth(const settings::Option& o)
        {
            float width = 0.0f;
            auto measure = [&](double v) { width = std::max(width, ImGui::CalcTextSize(Upper(settings::FormatValue(o.id, v).c_str()).c_str()).x); };
            switch (o.type)
            {
            case settings::Type::Bool:
                measure(0);
                measure(1);
                break;
            case settings::Type::Enum:
                for (const auto& e : o.entries)
                    measure(e.value);
                break;
            case settings::Type::Key:
                width = 2 * KeyCellWidth() + KeyCellGap();
                break;
            default:
                measure(o.min);
                measure(o.max);
                measure(settings::GetStoredValue(o.id));
                break;
            }
            return width;
        }

        // Every change the menu makes goes to the log (a player's report
        // then says what they changed and when, and tests read it).
        void LogChange(const settings::Option& o, double from, settings::SetResult result)
        {
            const char* how = "";
            switch (result)
            {
            case settings::SetResult::Applied: how = "applied now"; break;
            case settings::SetResult::Deferred: how = "at the next launch"; break;
            case settings::SetResult::Overridden: how = "saved; the environment keeps this run's value"; break;
            case settings::SetResult::Unchanged: return;
            case settings::SetResult::Invalid: how = "invalid"; break;
            }
            menu_sounds::Play(menu_sounds::Sound::Change);
            fprintf(stderr, "[settings] menu: %s.%s %s -> %s (%s)\n", o.table, o.key, settings::FormatValue(o.id, from).c_str(),
                settings::FormatValue(o.id, settings::GetStoredValue(o.id)).c_str(), how);
        }

        void Change(const settings::Option& o, double stored, int delta, bool wrap)
        {
            double next = settings::StepValue(o.id, stored, delta);
            // A (or a click) on an enum at its last entry starts over.
            if (wrap && next == stored && o.type == settings::Type::Enum && !o.entries.empty())
                next = delta > 0 ? o.entries.front().value : o.entries.back().value;
            if (next != stored)
                LogChange(o, stored, settings::Set(o.id, next));
        }

        float RowHeight()
        {
            return ImGui::GetFrameHeight() + (s_category == Category::Codes ? 2.0f : 10.0f) * Scale();
        }

        // Rows keep their text kSpaceL inside the highlight, which spans the
        // table: the label from its left edge, and the right arrow's point
        // from its right edge (ImGui::RenderArrow draws the point a fifth of
        // the font inside the glyph box centred in the square button).
        float LabelInset()
        {
            return kSpaceL * Scale();
        }

        float ValueInset()
        {
            float arrow = ImGui::GetFrameHeight(), font = ImGui::GetFontSize();
            return std::max(0.0f, LabelInset() - ((arrow - font) * 0.5f + font * 0.2f));
        }

        // The size of the rows' face (Barlow Condensed, set narrower than
        // the body text's Roboto at the same size).
        constexpr float kRowFontSize = 26.0f;
        // The game's settings rows: orange labels; the chosen one white.
        constexpr ImU32 kRowOrange = IM_COL32(242, 154, 34, 255);
        constexpr ImU32 kRowWhite = IM_COL32(246, 242, 232, 255);
        // The help line's and the dialogs' text (Barlow Condensed Bold Italic).
        constexpr float kTextFontSize = 24.0f;

        // A row's label, drawn here rather than by its Selectable (after it:
        // the highlight is the Selectable's item rect). As the game's
        // Settings screen: an orange label over a dark rule; the chosen row
        // white between two yellow lines.
        float RowLabel(ImVec2 pos, const char* label, bool enabled = true)
        {
            ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            float mid = (min.y + max.y) * 0.5f;
            bool focused = ImGui::IsItemFocused();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (focused)
                burnout::SelectorLines(draw, min, max);
            else
                burnout::RowRule(draw, min.x, max.x, max.y - 2 * Scale());
            const ImU32 color = !enabled ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : focused ? kRowWhite : kRowOrange;
            const std::string text = Upper(label);
            const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x - LabelInset());
            const float size = ImGui::GetFontSize();
            const ImVec2 measured = ImGui::CalcTextSize(text.c_str());
            const float fit = measured.x > width ? std::max(size * .72f, size * width / measured.x) : size;
            draw->PushClipRect(pos, {pos.x + width, max.y}, true);
            draw->AddText(ImGui::GetFont(), fit, {pos.x, mid - fit * .5f}, color, text.c_str());
            draw->PopClipRect();
            return mid;
        }

        // SETTINGS over [BURNOUT REVENGE FOR MAC], as the game heads its
        // own (DRIVER DETAILS over [SETTINGS]) on a dark band, and the keys
        // that open it on the right.
        void MenuTitle(const char* hint)
        {
            const float s = Scale();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float right = at.x + ImGui::GetContentRegionAvail().x;
            ImGui::PushFont(burnout::DisplayFont(), 46.0f);
            const ImVec2 title = ImGui::CalcTextSize(locale::Text("SETTINGS"));
            ImGui::PopFont();
            ImGui::PushFont(burnout::LabelFont(), 21.0f);
            const char* sub = "[BURNOUT REVENGE FOR MAC]";
            const ImVec2 subSize = ImGui::CalcTextSize(sub);
            ImGui::PopFont();
            const float subTop = at.y + title.y - 4 * s;
            const float bandTop = at.y - 2 * s, bandBottom = subTop + subSize.y + 4 * s;
            // The heading starts after the band's chevrons.
            const ImVec2 text(burnout::TitleBand(draw, ImVec2(at.x, bandTop), ImVec2(at.x + std::max(title.x, subSize.x) + 40 * s, bandBottom)),
                at.y);
            ImGui::PushFont(burnout::DisplayFont(), 46.0f);
            draw->AddText(ImVec2(text.x + 2 * s, text.y + 2 * s), IM_COL32(0, 0, 0, 200), locale::Text("SETTINGS"));
            draw->AddText(text, kRowWhite, locale::Text("SETTINGS"));
            ImGui::PopFont();
            ImGui::PushFont(burnout::LabelFont(), 21.0f);
            draw->AddText(ImVec2(text.x + 4 * s, subTop), kRowOrange, sub);
            if (hint)
                draw->AddText(ImVec2(right - ImGui::CalcTextSize(hint).x, at.y + 6 * s), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, subTop + subSize.y - at.y + 6 * s));
        }

        // The footer's legend as the game writes its own: "Back" and the
        // button, right-aligned on this line.
        void Legend(std::initializer_list<std::pair<const char*, const char*>> items)
        {
            burnout::Hints(items);
        }

        void OptionRow(const settings::Option& o, float valueWidth, bool focus)
        {
            float s = Scale();
            const float rowHeight = RowHeight();
            double stored = settings::GetMenuValue(o.id);
            // Overrides show their effective value and cannot be edited this run.
            // Game Mode always runs fullscreen: shown, not changeable.
            const bool locked = settings::EnvOverride(o.id) || (o.id == Id::Fullscreen && video::FullscreenLocked()) ||
                ((o.id == Id::Sharpening || o.id == Id::AntiAliasing) && video::UsesMetalFx()) ||
                (o.type == settings::Type::Enum && o.entries.size() == 1);
            if (locked && (o.id == Id::Fullscreen || o.id == Id::AntiAliasing))
                stored = 1.0;  // Game Mode's fullscreen; MetalFX's anti-aliasing
            ImGui::PushID(int(o.id));
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableNextColumn();

            if (focus)
                ImGui::SetKeyboardFocusHere();
            // The highlight spans the row wherever the label starts.
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LabelInset());
            ImVec2 labelPos = ImGui::GetCursorScreenPos();
            bool activated = ImGui::Selectable("##row", false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowHeight));
            bool focused = ImGui::IsItemFocused();
            bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);
            if (focused || (hovered && s_described == Id::Count))
                s_described = o.id;
            const float mid = RowLabel(labelPos, locale::Text(o.label));

            // Markers after the label: set by the environment, waits for a restart.
            {
                float x = labelPos.x + ImGui::CalcTextSize(Upper(locale::Text(o.label)).c_str()).x + 10.0f * s;
                ImDrawList* draw = ImGui::GetWindowDrawList();
                auto marker = [&](const char* text, ImU32 color) {
                    text = locale::Text(text);
                    ImGui::PushFont(nullptr, 15.0f);
                    ImVec2 size = ImGui::CalcTextSize(text);
                    float y = CentredTextTop(mid);
                    ImVec2 pad(5.0f * s, 1.0f * s);
                    draw->AddRectFilled(ImVec2(x, y - pad.y), ImVec2(x + size.x + pad.x * 2, y + size.y + pad.y),
                        (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 0x40), 3.0f * s);
                    draw->AddText(ImVec2(x + pad.x, y), color, text);
                    ImGui::PopFont();
                    x += size.x + pad.x * 2 + 6.0f * s;
                };
                if (settings::EnvOverride(o.id))
                    marker("ENV", ImGui::GetColorU32(ImGuiCol_TextDisabled));
                if (settings::RestartPending(o.id))
                    marker("RESTART", ImGui::GetColorU32(ImGuiCol_PlotLines));
                // The row is the saved choice; the system (the green button)
                // changed the window this session: say what it is now.
                if (o.id == Id::Fullscreen && !locked && video::FullscreenWindowDiffers())
                    marker(video::WindowFullscreen() ? "NOW FULLSCREEN" : "NOW WINDOWED", ImGui::GetColorU32(ImGuiCol_TextDisabled));
            }

            int delta = 0;
            bool wrap = false;
            if (focused)
            {
                if (Pressed(ImGuiKey_GamepadDpadLeft) || Pressed(ImGuiKey_GamepadLStickLeft) || Pressed(ImGuiKey_LeftArrow))
                    delta = -1;
                if (Pressed(ImGuiKey_GamepadDpadRight) || Pressed(ImGuiKey_GamepadLStickRight) || Pressed(ImGuiKey_RightArrow))
                    delta = 1;
            }
            // A (or Enter) on an on/off or a choice row moves it on. A click
            // or tap on the row only selects it (and shows what it does): the
            // arrows change it, so a touch that only meant to look, or to
            // scroll, changes nothing. A pointer activates on its release.
            const bool byPointer = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            if (activated && !byPointer && (o.type == settings::Type::Bool || o.type == settings::Type::Enum))
            {
                delta = 1;
                wrap = true;
            }

            // The value, between arrows (mouse only: the controller changes
            // the focused row with left/right, so the arrows stay out of its way).
            ImGui::TableNextColumn();
            float arrow = ImGui::GetFrameHeight();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowHeight - arrow) * 0.5f);
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            // The game's yellow chevrons on the chosen row; faint elsewhere,
            // still there for a pointer.
            ImGui::PushStyleColor(ImGuiCol_Text, focused ? ImVec4(0.96f, 0.71f, 0.15f, 1) : ImVec4(1, 1, 1, 0.14f));
            ImGui::BeginDisabled(locked);
            if (ImGui::ArrowButton("##less", ImGuiDir_Left))
                delta = -1;
            ImGui::SameLine(0, 0);
            ImVec2 valueMin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(valueWidth, arrow));
            ImGui::SameLine(0, 0);
            if (ImGui::ArrowButton("##more", ImGuiDir_Right))
                delta = 1;
            ImGui::EndDisabled();
            ImGui::PopStyleColor(2);
            ImGui::PopItemFlag();

            std::string text = Upper(settings::FormatValue(o.id, stored).c_str());
            ImVec2 size = ImGui::CalcTextSize(text.c_str());
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImU32 color = locked ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : kRowWhite;
            draw->AddText(ImVec2(valueMin.x + (valueWidth - size.x) * 0.5f, CentredTextTop(mid)), color, text.c_str());
            // Numbers get the game's volume bar under them: orange blocks
            // up to where they sit in their range, outlines after.
            if ((o.type == settings::Type::Int || o.type == settings::Type::Float) && o.max > o.min)
            {
                float t = float(std::clamp((stored - o.min) / (o.max - o.min), 0.0, 1.0));
                constexpr int kBlocks = 20;
                const float pitch = valueWidth / kBlocks, w = pitch * 0.7f, h = 7.0f * s;
                const float y = valueMin.y + arrow - h + 2.0f * s;
                const int lit = int(std::round(t * kBlocks));
                for (int i = 0; i < kBlocks; i++)
                {
                    ImVec2 a(valueMin.x + i * pitch + (pitch - w) * 0.5f, y), b(a.x + w, y + h);
                    if (i < lit)
                        draw->AddRectFilled(a, b, kRowOrange, 1.0f * s);
                    else
                        draw->AddRect(a, b, IM_COL32(200, 200, 200, 110), 1.0f * s, 0, 1.0f * s);
                }
            }

            if (delta && !locked)
                Change(o, stored, delta, wrap);
            ImGui::PopID();
        }

        // A Controls row: the two keys that drive the control, as two cells.
        // Left and Right choose a cell (the focused row's choice; another
        // row starts on its first); A or Enter waits for a key for it, which
        // SettingsMenuTakesKey takes from the event loop before ImGui sees
        // it; Y or Backspace empties it; X or Delete puts both keys back to
        // their defaults. A click or tap only selects the row, and the cell
        // under the pointer, as on the other pages. The wait follows the
        // focus (the D-pad moving on ends it) and B ends it (below, in
        // DrawSettings, where B would otherwise close the menu).
        void KeyRow(const hid::keymap::Entry& e, float valueWidth, bool focus)
        {
            const float s = Scale();
            const float rowHeight = RowHeight();
            const settings::Option& o = settings::Info(e.id);
            const Id ids[2] = { e.id, e.id2 };
            ImGui::PushID(int(e.id));
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableNextColumn();
            if (focus)
                ImGui::SetKeyboardFocusHere();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LabelInset());
            ImVec2 labelPos = ImGui::GetCursorScreenPos();
            bool activated = ImGui::Selectable("##row", false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowHeight));
            bool focused = ImGui::IsItemFocused();
            if (focused || (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem) && s_described == Id::Count))
                s_described = e.id;
            const float mid = RowLabel(labelPos, locale::Text(o.label));

            if (focused && s_slotRow != e.id)
            {
                s_slotRow = e.id;
                s_slot = 0;
            }
            const bool waiting = s_waiting == e.id || s_waiting == e.id2;
            if (waiting && !focused)
                EndWait();

            // The cells, where other pages put their value (the arrows'
            // room kept so the columns line up from page to page).
            ImGui::TableNextColumn();
            const float arrow = ImGui::GetFrameHeight();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowHeight - arrow) * 0.5f);
            const ImVec2 min = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(valueWidth + arrow * 2, arrow));
            const float gap = KeyCellGap(), cell = (valueWidth - gap) * 0.5f;
            const float cellX[2] = { min.x + arrow, min.x + arrow + cell + gap };

            const bool byPointer = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            if (activated && byPointer)
            {
                const float x = ImGui::GetMousePos().x;
                for (int i = 0; i < 2; i++)
                    if (x >= cellX[i] && x < cellX[i] + cell)
                        s_slot = i;
            }
            else if (activated)
            {
                s_waiting = ids[s_slot];
                s_refused = SDL_SCANCODE_UNKNOWN;
            }
            else if (focused && s_waiting == Id::Count)
            {
                if (Pressed(ImGuiKey_GamepadDpadLeft) || Pressed(ImGuiKey_GamepadLStickLeft) || Pressed(ImGuiKey_LeftArrow))
                    s_slot = 0;
                if (Pressed(ImGuiKey_GamepadDpadRight) || Pressed(ImGuiKey_GamepadLStickRight) || Pressed(ImGuiKey_RightArrow))
                    s_slot = 1;
                if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
                {
                    const settings::Option& slot = settings::Info(ids[s_slot]);
                    LogChange(slot, settings::GetStoredValue(slot.id), settings::Set(slot.id, settings::keynames::kNone));
                }
                if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                    for (Id id : ids)
                        LogChange(settings::Info(id), settings::GetStoredValue(id), settings::Reset(id));
            }

            ImDrawList* draw = ImGui::GetWindowDrawList();
            for (int i = 0; i < 2; i++)
            {
                const bool waitingHere = s_waiting == ids[i];
                const bool chosen = focused && s_slot == i;
                const double stored = settings::GetStoredValue(ids[i]);
                const std::string text = waitingHere ? locale::Text(kPressAKey) : KeyLabel(stored);
                const ImVec2 size = ImGui::CalcTextSize(text.c_str());
                ImU32 color;
                if (waitingHere)
                {
                    PushAccentText();
                    color = ImGui::GetColorU32(ImGuiCol_Text);
                    PopAccentText();
                }
                else if (chosen)
                    color = ImGui::GetColorU32(ImGuiCol_PlotLines);
                else
                    color = ImGui::GetColorU32(stored == double(settings::keynames::kNone) ? ImGuiCol_TextDisabled : ImGuiCol_Text);
                const ImVec2 at(cellX[i] + (cell - size.x) * 0.5f, CentredTextTop(mid));
                draw->AddText(at, color, text.c_str());
                if (chosen)  // underlined too, so an empty cell shows which it is
                    draw->AddLine(ImVec2(at.x, at.y + size.y + 2.0f * s), ImVec2(at.x + size.x, at.y + size.y + 2.0f * s), color, 1.0f * s);
            }
            ImGui::PopID();
        }

        const char* BugReportValue()
        {
            switch (report::GetBugReportState())
            {
            case report::BugReportState::Saving: return "Saving...";
            case report::BugReportState::Saved: return "Saved";
            case report::BugReportState::Failed: return "Failed";
            default: return "Save";
            }
        }

        // An action, not an option: a label and what it shows now (no
        // arrows). True when A (or a click) pressed it. Not `enabled`: dim,
        // skipped by the controller's cursor, A and clicks do nothing.
        bool ActionRow(const char* id, const char* label, const char* value, float valueWidth, bool& described,
            bool enabled = true)
        {
            const float rowHeight = RowHeight();
            ImGui::PushID(id);
            ImGui::BeginDisabled(!enabled);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableNextColumn();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LabelInset());
            ImVec2 labelPos = ImGui::GetCursorScreenPos();
            bool activated = ImGui::Selectable("##row", false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowHeight));
            bool focused = ImGui::IsItemFocused();
            if (focused || (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem | ImGuiHoveredFlags_AllowWhenDisabled)
                               && s_described == Id::Count))
                described = true;
            const float mid = RowLabel(labelPos, label, enabled);
            ImGui::TableNextColumn();
            float arrow = ImGui::GetFrameHeight();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowHeight - arrow) * 0.5f);
            ImVec2 min = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(valueWidth + arrow * 2, arrow));
            const std::string text = Upper(value);
            ImVec2 size = ImGui::CalcTextSize(text.c_str());
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + arrow + (valueWidth - size.x) * 0.5f, CentredTextTop(mid)),
                enabled ? kRowWhite : ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
            ImGui::EndDisabled();
            ImGui::PopID();
            activated = activated && enabled;
            if (activated)
                menu_sounds::Play(menu_sounds::Sound::Select);
            return activated;
        }

        void ModsSection(const char* label)
        {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, RowHeight() * .6f);
            ImGui::TableNextColumn();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LabelInset());
            ImGui::PushFont(burnout::LabelFont(), 18.f);
            ImGui::TextDisabled("%s", Upper(label).c_str());
            ImGui::PopFont();
            ImGui::TableNextColumn();
        }

        void MaddenRow(float width)
        {
            using game::madden::Status;
            const auto state = game::madden::GetStatus();
            const char* value = state == Status::Unlocked ? "Unlocked" :
                state == Status::Pending ? "Unlocking..." : state == Status::Ready ? "Unlock" : "Open World Tour";
            if (ActionRow("madden-van", "Unlock Madden Van", value, width, s_describedMadden, state == Status::Ready))
                game::madden::Request();
        }

        std::string RewindStateText()
        {
            const auto status=game::rewind::ring::GetStatus();
            using game::rewind::ring::State;
            if (!settings::GetBool(Id::RewindEnabled)) return locale::Text("Turn on Rewind to collect history.");
            if (status.state==State::Restoring) return locale::Text("Rewind in progress.");
            if (status.state==State::Saving) return locale::Text("History resets while the game saves.");
            if (status.state==State::Failed) return locale::Text("Rewind stopped after an error. Turn it off and on to try again.");
            if (status.state==State::Unsupported || status.state==State::Disabled) return locale::Text("Unavailable here. Start a supported solo World Tour event.");
            return locale::Format("Available history: {} seconds. New history replaces the discarded future.",status.seconds);
        }

        void RewindRows(float width)
        {
            const auto status=game::rewind::ring::GetStatus();
            const unsigned seconds=unsigned(settings::GetInt(Id::RewindDuration));
            const bool ready=settings::GetBool(Id::RewindEnabled) && (status.readyMask&(1u<<(seconds/10-1)));
            // Keep the row focusable so controller users can read why it is
            // unavailable; activation never queues an invalid request.
            if (ActionRow("rewind-now","Rewind Now",ready ? "Ready":"Not ready",width,s_rewindHelp,true) && ready)
                TriggerRewind(seconds);
            constexpr std::array ids{Id::RewindChord10,Id::RewindChord20,Id::RewindChord30};
            bool anyEditable=false;
            for (const auto id:ids)
            {
                bool rowHelp=false;
                const auto mask=uint16_t(settings::GetInt(id));
                const bool valid=hid::rewind::ValidChord(mask),locked=settings::EnvOverride(id);
                if (ActionRow(settings::Info(id).key,settings::Info(id).label,(s_chordRecording && s_chordSetting==id) ? "Release to set":locked ? "ENV":valid ? "":"Not set",width,rowHelp,!locked))
                { s_chordSetting=id;s_chordRecording=true;s_chordArmed=false;s_chordPeak=0;s_chordMessage.clear(); }
                if (!(s_chordRecording && s_chordSetting==id) && valid && !locked)
                {
                    constexpr std::pair<uint16_t,const char*> buttons[]={
                        {0x100,"LB"},{0x200,"RB"},{0x4000,"X"},{0x8000,"Y"},{0x2000,"B"},{0x1000,"A"},
                        {0x40,"LS"},{0x80,"RS"},{0x20,"BACK"},{0x10,"START"},{1,"UP"},{2,"DOWN"},{4,"LEFT"},{8,"RIGHT"}};
                    const float h=ImGui::GetFrameHeight()*.8f,gap=12.f*Scale();
                    float total=0;unsigned count=0;
                    for (auto [bit,name]:buttons) if (mask&bit) { total+=burnout::ButtonGlyph(nullptr,{},h,name,true);++count; }
                    total+=gap*float(count-1);
                    const auto min=ImGui::GetItemRectMin(),size=ImGui::GetItemRectSize();
                    ImVec2 at{min.x+(size.x-total)/2,min.y+(size.y-h)/2};
                    auto* draw=ImGui::GetWindowDrawList();
                    unsigned drawn=0;
                    for (auto [bit,name]:buttons) if (mask&bit)
                    {
                        if (drawn++) { draw->AddText({at.x+2.f*Scale(),at.y},kRowWhite,"+");at.x+=gap; }
                        at.x+=burnout::ButtonGlyph(draw,at,h,name,true);
                    }
                }
                s_chordHelp|=rowHelp;
                anyEditable|=!locked;
            }
            if (s_chordRecording)
            {
                const uint16_t held=hid::RawButtons();
                if (!held) s_chordArmed=true;
                if (s_chordArmed)
                {
                    if (std::popcount(held)>std::popcount(s_chordPeak)) s_chordPeak=held;
                    if (!held && s_chordPeak)
                    {
                        bool overlaps=false;
                        for (const auto id:ids) if (id!=s_chordSetting)
                        {
                            const auto other=uint16_t(settings::GetInt(id));
                            if (hid::rewind::ValidChord(other) && ((s_chordPeak&other)==other || (s_chordPeak&other)==s_chordPeak)) overlaps=true;
                        }
                        if (overlaps) s_chordMessage=locale::Text("This shortcut overlaps another duration. Clear or change the other shortcut first.");
                        else if (hid::rewind::ValidChord(s_chordPeak))
                            LogChange(settings::Info(s_chordSetting),settings::GetStoredValue(s_chordSetting),settings::Set(s_chordSetting,s_chordPeak));
                        else s_chordMessage=locale::Text("Use two or three buttons. Back + Start is reserved for Settings.");
                        s_chordRecording=false;
                    }
                }
            }
            bool clearHelp=false;
            if (ActionRow("rewind-chord-clear","Clear Controller Shortcuts","Clear",width,clearHelp,anyEditable) && !s_chordRecording)
                for (const auto id:ids) if (!settings::EnvOverride(id))
                    LogChange(settings::Info(id),settings::GetStoredValue(id),settings::Set(id,0));
            s_chordHelp|=clearHelp;
        }

        // Settings > Wheel > Calibrate. The wheels we know say which axis
        // each pedal is, and where the axes rest tells a pedal from the
        // wheel itself (hid/wheel_map.h) - but three identical springs on a
        // wheel we don't know can only be told apart by someone pressing
        // them, and which way the wheel turns only by someone turning it.
        //
        // So: one step at a time, each showing what it has found and
        // waiting for A to accept it. B gives up. The help line shows what
        // every axis reads as it happens, so a wheel can be checked over
        // without leaving the page.
        using Step = hid::wheel::Step;

        void CancelCalibrate()
        {
            s_calibrating = false;
            hid::wheel::CancelAssign();
        }

        void AskFor(Step step)
        {
            if (size_t(step) >= hid::wheel::kStepCount)
            {
                s_calibrating = false;
                fprintf(stderr, "[settings] menu: calibrate: done\n");
                return;
            }
            s_calibrating = true;
            s_calStep = step;
            hid::wheel::BeginAssign(step);
        }

        // Every axis and what it reads now, for the help line.
        std::string WheelReadings()
        {
            const hid::wheel::Assignment axes = hid::wheel::Axes();
            const hid::wheel::Reading now = hid::wheel::Last();
            std::string text = axes.steer < 0
                ? "Steering: none"
                : locale::Format("Steering axis {}{}: {:+.0f}%", axes.steer,
                      settings::GetBool(Id::InvertSteering) ? " inverted" : "", double(now.steer) * 100.0);
            struct Row { const char* name; int axis; float value; };
            for (const Row& row : { Row{ "Throttle", axes.throttle, now.throttle }, Row{ "Brake", axes.brake, now.brake },
                                    Row{ "Clutch", axes.clutch, now.clutch } })
                text += row.axis < 0 ? locale::Format("   {}: none", row.name)
                                     : locale::Format("   {} {}: {:.0f}%", row.name, row.axis, double(row.value) * 100.0);
            return text;
        }

        void CalibrateRow(float valueWidth)
        {
            // While it runs, the value column counts the steps off: the
            // names are too long for it, and the help line has them.
            const std::string value = s_calibrating
                ? locale::Format("{}/{}", size_t(s_calStep) + 1, hid::wheel::kStepCount)
                : std::string("Start");
            const bool activated = ActionRow("wheel-calibrate", "Calibrate", value.c_str(), valueWidth, s_describedCal);
            if (!s_calibrating)
            {
                if (activated)
                    AskFor(Step::SteerLeft);
                return;
            }
            // A keeps what the step found, wherever the cursor is: it may
            // have wandered off the row while both hands were on the wheel.
            // Nothing happens until the step has something.
            const bool confirm = activated || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Space, false);
            if (confirm && hid::wheel::ConfirmAssign())
                AskFor(Step(size_t(s_calStep) + 1));
        }

        // A (or a click) saves the report.
        void BugReportRow(float valueWidth)
        {
            if (ActionRow("bug-report", "Save Bug Report", BugReportValue(), valueWidth, s_describedBugReport))
                report::SaveBugReport();
        }

        // A (or a click) asks GitHub, and the dialog shows the answer.
        void UpdateRow(float valueWidth)
        {
            if (ActionRow("check-updates", "Check for Updates", UpdateRowValue(), valueWidth, s_describedUpdate))
                OpenUpdateDialog();
        }

        bool QuitPromptOpen()
        {
#if BR_QUIT_GAME
            return ImGui::IsPopupOpen(kQuitPopup);
#else
            return false;
#endif
        }

#if BR_QUIT_GAME
        // Quit Game's prompt (quit_game.h), at the settings window's top
        // level like the other dialogs. True while it is open: B is its, not
        // the menu's.
        bool DrawQuitPrompt()
        {
            if (!ImGui::IsPopupOpen(kQuitPopup))
            {
                if (s_quit.Dismiss() == quit::Outcome::Cancelled)
                    fprintf(stderr, "[ui] Quit Game: cancelled (the prompt closed)\n");
                return false;
            }
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            burnout::PushDialogStyle();
            const bool shown = ImGui::BeginPopupModal(kQuitPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
            burnout::PopDialogStyle();
            if (!shown)
            {
                if (s_quit.Dismiss() == quit::Outcome::Cancelled)
                    fprintf(stderr, "[ui] Quit Game: cancelled (the prompt closed)\n");
                return false;
            }
            burnout::DialogFrame();
            s_quit.Shown();
            const float width = burnout::kDialogWidth * Scale();
            burnout::DialogText("Quit Burnout Revenge? Progress since your last save will be lost.", width);
            ImGui::Dummy(ImVec2(0, 10 * Scale()));
            const bool quitPressed = burnout::DialogOption("Quit", width);
            if (s_quit.TakeFocus())
                ImGui::SetKeyboardFocusHere();  // Cancel (quit::Prompt::kDefaultFocus)
            const bool cancelPressed = burnout::DialogOption("Cancel", width);
            ImGui::SetItemDefaultFocus();
            quit::Outcome outcome = quit::Outcome::None;
            const char* how = "";
            if (cancelPressed)
            {
                outcome = s_quit.Press(quit::Button::Cancel);
                how = "Cancel";
            }
            else if (quitPressed)
                outcome = s_quit.Press(quit::Button::Quit);
            else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                outcome = s_quit.Back();
                how = "B or Escape";
            }
            if (outcome == quit::Outcome::Cancelled)
            {
                fprintf(stderr, "[ui] Quit Game: cancelled (%s)\n", how);
                ImGui::CloseCurrentPopup();
            }
            else if (outcome == quit::Outcome::Quit)
            {
                // The way out a window close takes: RunFrame returns false on
                // its next poll and main() shuts the game down.
                fprintf(stderr, "[ui] Quit Game: quitting (as closing the window does)\n");
                ImGui::CloseCurrentPopup();
                SDL_Event e{};
                e.type = SDL_EVENT_QUIT;
                e.quit.timestamp = SDL_GetTicksNS();
                if (!SDL_PushEvent(&e))
                {
                    fprintf(stderr, "[ui] Quit Game: couldn't queue the quit: %s\n", SDL_GetError());
                    Toast("Couldn't quit. Close the game's window instead.");
                    s_quit.QuitFailed();
                }
            }
            ImGui::Dummy(ImVec2(0, 6 * Scale()));
            burnout::Hints({ { "Select", "A" }, { "Back", "B" } });
            burnout::EndDialog();
            return true;
        }
#endif

        // Audio's Music Folder row: how many files My Music would play
        // (from the shared background index), and A opens the
        // folder in Finder, made first.
        std::string MusicFolderValue()
        {
            const auto library = user::my_music::Library();
            if (!library->ready) return locale::Text("Scanning...");
            const auto count = library->tracks.size();
            if (count == 0 && !library->error.empty()) return locale::Text("Folder unavailable");
            if (library->limited) return locale::Format("{} files (limit)", count);
            return count == 0 ? locale::Text("Empty") : locale::Format("{} {}", count, locale::Text(count == 1 ? "file" : "files"));
        }

        void MusicFolderRow(float valueWidth)
        {
            // Only for My Music: with EA Trax there's no folder to pick.
            const bool myMusic = settings::GetInt(Id::Music) == int32_t(settings::MusicSource::MyMusic);
            if (ActionRow("music-folder", "Music Folder", MusicFolderValue().c_str(), valueWidth, s_describedMusicFolder, myMusic))
            {
                std::error_code error;
                std::filesystem::create_directories(user::my_music::Folder(), error);
                SDL_OpenURL(("file://" + user::my_music::Folder().string()).c_str());
            }
        }

        // Settings > Online's rows besides its two options.
        settings::OnlineServer StoredServer()
        {
            return settings::OnlineServer(int32_t(settings::GetStoredValue(Id::OnlineServer)));
        }

        std::string GamertagRowValue()
        {
            const std::string tag = settings::GetText(settings::TextId::Gamertag);
            if (!tag.empty())
                return tag;
            const char* user = std::getenv("USER");
            return online::DefaultGamertagFor(user ? user : "");
        }

        std::string CheckRowValue()
        {
            if (s_checking)
                return "Checking...";
            std::lock_guard lock(s_checkMutex);
            if (!s_checkResult || s_checkedServer != online::ChosenServer())
                return "Check";
            return s_checkResult->ok ? "OK" : "Failed";
        }

        void StartCheck()
        {
            if (s_checking.exchange(true))
                return;
            const std::string server = online::ChosenServer();
            std::thread([server] {
                online::CheckResult result = online::CheckServer(server);
                fprintf(stderr, "[settings] menu: check server \"%s\": %s\n", server.c_str(), result.text.c_str());
                {
                    std::lock_guard lock(s_checkMutex);
                    s_checkResult = std::move(result);
                    s_checkedServer = server;
                }
                s_checking = false;
            }).detach();
        }

        void OnlineRows(float valueWidth)
        {
            if (ActionRow("gamertag", "Gamertag", GamertagRowValue().c_str(), valueWidth, s_describedGamertag))
            {
                s_textEditing = settings::TextId::Gamertag;
                s_openText = true;
            }
            const bool own = StoredServer() == settings::OnlineServer::Own;
            std::string address = settings::GetText(settings::TextId::ServerAddress);
            if (ActionRow("server-address", "Server Address", address.empty() ? "Not set" : address.c_str(), valueWidth,
                    s_describedServerAddress, own))
            {
                s_textEditing = settings::TextId::ServerAddress;
                s_openText = true;
            }
            if (ActionRow("check-server", "Check Server", CheckRowValue().c_str(), valueWidth, s_describedCheck,
                    StoredServer() != settings::OnlineServer::Lan && !s_checking))
                StartCheck();
        }

        // The text dialog of Gamertag and Server Address: typed on a
        // keyboard (Enter keeps it, Esc leaves it as it was).
        bool DrawTextDialog(float s)
        {
            if (s_openText)
            {
                const std::string now = settings::GetText(s_textEditing);
                std::snprintf(s_textBuffer, sizeof(s_textBuffer), "%s", now.c_str());
                ImGui::OpenPopup("online-text");
                s_openText = false;
            }
            if (!ImGui::IsPopupOpen("online-text"))
                return false;
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            burnout::PushDialogStyle();
            const bool shown = ImGui::BeginPopupModal("online-text", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
            burnout::PopDialogStyle();
            if (!shown)
                return true;
            burnout::DialogFrame();
            const bool gamertag = s_textEditing == settings::TextId::Gamertag;
            const float width = burnout::kDialogWidth * s;
            burnout::DialogText(gamertag ? "Your gamertag: up to 15 letters, digits, - and _. Empty: Player and four digits."
                                         : "Your server's host name or IPv4 address.", width);
            ImGui::Dummy(ImVec2(0, 8 * s));
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(width);
            bool accept = ImGui::InputText("##text", s_textBuffer, gamertag ? online::kMaxGamertag + 1 : sizeof(s_textBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            ImGui::Dummy(ImVec2(0, 10 * s));
            accept |= burnout::DialogOption("OK", width);
            if (accept)
            {
                std::string value = s_textBuffer;
                if (gamertag && !value.empty())
                    value = online::SanitizeGamertag(value);
                settings::SetText(s_textEditing, value);
                fprintf(stderr, "[settings] menu: online %s = \"%s\" (applies at the next launch)\n", settings::TextKey(s_textEditing), value.c_str());
                ImGui::CloseCurrentPopup();
            }
            if (burnout::DialogOption("Cancel", width) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
                ImGui::CloseCurrentPopup();
            ImGui::Dummy(ImVec2(0, 6 * s));
            burnout::Hints({ { "Select", "A" }, { "Back", "B" } });
            burnout::EndDialog();
            return true;
        }

        void HelpLine()
        {
            if (s_chordHelp || s_chordRecording)
            {
                ImGui::TextWrapped("%s",locale::Text(s_chordRecording ? "Release all buttons, hold two or three together, then release to save. Esc cancels.":
                    "Hold your shortcut for 0.3 seconds during play to rewind once. Release every shortcut button before using it again. Individual buttons keep their normal actions."));
                if (!s_chordMessage.empty()) ImGui::TextWrapped("%s",s_chordMessage.c_str());
                return;
            }
            if (s_rewindHelp)
            { ImGui::TextWrapped("%s",RewindStateText().c_str());return; }
            if (s_category == Category::Achievements) { AchievementHelp(); return; }
            if (s_describedGamertag)
            {
                ImGui::TextWrapped("%s", locale::Text("Your name online. Each player needs a different one: the lobby knows players by it. "
                                   "Changes apply after restarting the game."));
                return;
            }
            if (s_describedServerAddress)
            {
                ImGui::TextWrapped("%s", locale::Text("The host name or IPv4 address of your own server (burnout-lobby, which comes with the "
                                   "game's source). Set Server to My Server to use it. Changes apply after restarting the game."));
                return;
            }
            if (s_describedCheck)
            {
                std::lock_guard lock(s_checkMutex);
                if (s_checking)
                    ImGui::TextWrapped(locale::Text("Asking %s..."), online::ChosenServer().c_str());
                else if (s_checkResult && s_checkedServer == online::ChosenServer())
                    ImGui::TextWrapped("%s", s_checkResult->text.c_str());
                else
                    ImGui::TextWrapped("%s", locale::Text("Ask the chosen server whether it is up, as signing in does. Nothing is sent but the question."));
                return;
            }
            if (s_describedMusicFolder)
            {
                // The home folder as ~: the path fits the line.
                std::string folder = user::my_music::Folder().string();
                if (const char* home = std::getenv("HOME"); home && *home && folder.starts_with(home))
                    folder = "~" + folder.substr(std::strlen(home));
                if (settings::GetInt(Id::Music) != int32_t(settings::MusicSource::MyMusic))
                    ImGui::TextWrapped(locale::Text("Set Music to My Music to use your own files from %s."), folder.c_str());
                else
                    ImGui::TextWrapped(locale::Text("Put MP3, AAC, ALAC, FLAC, WAV or AIFF files (subfolders too) in %s. "
                                       "A opens the folder."), folder.c_str());
                return;
            }
            if (s_describedSaveBackups)
            {
                ImGui::TextWrapped("%s", SaveBackupsHelp());
                return;
            }
            if (s_describedLanguages)
            {
                ImGui::TextWrapped("%s", locale::Text("Import translations from your own European Xbox 360 disc image. Only language resources are copied; the game executable and saves stay as they are. Choose Language above and restart to apply."));
                std::lock_guard lock(s_languageMutex);
                if (!s_languageMessage.empty()) ImGui::TextWrapped("%s", locale::Text(s_languageMessage.c_str()));
                return;
            }
            if (s_describedAbout)
            {
                // GPL-3.0 section 5(d): the interface shows the legal
                // notices (NOTICE, THIRD_PARTY_NOTICES.md), on this row.
                ImGui::TextWrapped(locale::Text("%s %s. Free software under the GPL-3.0-or-later, with no warranty; third-party "
                                   "licenses come with the game."), game::kApplication, report::VersionString());
                ImGui::TextDisabled("%s", locale::Text("Copyright (C) 2026 project(u) and SpeedBreaker contributors. Not affiliated with Electronic Arts or Apple."));
                return;
            }
            if (s_describedAddons)
            {
                ImGui::TextWrapped("%s", locale::Text("View connected Burnout Revenge car packs or import your own Add-on folder. New packs connect on the next launch."));
                return;
            }
            if (s_describedUpdate)
            {
                UpdateRowHelp();
                return;
            }
            if (s_describedBugReport)
            {
#if defined(__APPLE__) && TARGET_OS_IOS
                ImGui::TextWrapped("%s", locale::Text("Saves the recent logs, crash and hang reports, your settings, a system summary and a screenshot "
                                   "in one .zip in the Files app (BurnoutRevenge folder). Nothing is uploaded: send the file yourself."));
#else
                ImGui::TextWrapped("%s", locale::Text("Saves the recent logs, crash and hang reports, your settings, a system summary and a screenshot "
                                   "in one .zip on the Desktop (or the game's data folder). Nothing is uploaded: send the file yourself."));
#endif
                std::string message = report::BugReportMessage();
                const bool saved = report::GetBugReportState() == report::BugReportState::Saved;
                // One line under the description (the footer has room for no
                // more): a path too long for it gives way to the file's name,
                // or to the reason it failed. The toast has the whole text.
                if (ImGui::CalcTextSize(message.c_str()).x > ImGui::GetContentRegionAvail().x)
                {
                    size_t cut = saved ? message.rfind('/') : message.rfind(": ");
                    if (saved && cut == std::string::npos && (cut = message.rfind(": ")) != std::string::npos)
                        cut++;  // iOS: "Saved to the Files app, BurnoutRevenge folder: <name>"
                    if (cut != std::string::npos)
                        message = saved ? std::string(locale::Text("Saved: ")) + message.substr(cut + 1) : std::string(locale::Text("Couldn't save the bug report: ")) + message.substr(cut + 2);
                }
                if (saved)
                {
                    PushAccentText();
                    ImGui::TextUnformatted(message.c_str());
                    PopAccentText();
                }
                else
                    ImGui::TextDisabled("%s", message.empty() ? " " : message.c_str());
                return;
            }
            if (s_describedCal)
            {
                if (s_calibrating)
                {
                    const hid::wheel::Candidate c = hid::wheel::Watch();
                    PushAccentText();
                    if (!c.found)
                        ImGui::Text(locale::Text("%zu of %zu, %s. %s. B stops here, keeping what you've set."), size_t(s_calStep) + 1,
                            hid::wheel::kStepCount, locale::Text(hid::wheel::StepName(s_calStep)), locale::Text(hid::wheel::StepAsk(s_calStep)));
                    else if (hid::wheel::StepIsButton(s_calStep))
                        ImGui::Text(locale::Text("%s: button %d. A keeps it, or press another."), locale::Text(hid::wheel::StepName(s_calStep)), c.button);
                    else
                        ImGui::Text(locale::Text("%s: axis %d at %+d, %.0f%% of its travel. A keeps it, or do it again."),
                            locale::Text(hid::wheel::StepName(s_calStep)), c.axis, int(c.full), double(c.moved) * 100.0);
                    PopAccentText();
                }
                else
                    ImGui::TextWrapped("%s", locale::Text("Teaches the game your wheel: both ends of its turn, each pedal, and a button for "
                                       "every control. It asks for one at a time and waits for A to keep what it saw. Only "
                                       "a wheel that comes out wrong needs this."));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", WheelReadings().c_str());
                ImGui::PopStyleColor();
                return;
            }
            if (s_describedMadden)
            {
                ImGui::TextWrapped("%s", locale::Text("Unlock the Madden Van for Crash events in the loaded Player File, just like opening the Madden NFL 06 trailer. Available in the World Tour menu. Save your Player File afterwards to keep it. Career medals and completion stay the same."));
                return;
            }
            if (s_described == Id::Count)
            {
                ImGui::TextDisabled(" ");
                ImGui::TextDisabled(" ");
                return;
            }
            const settings::Option& o = settings::Info(s_described);
            if (o.type == settings::Type::Key)
            {
                const hid::keymap::Entry& e = KeyEntry(o.id);
                if (s_waiting == e.id || s_waiting == e.id2)
                {
                    PushAccentText();
                    if (s_refused != SDL_SCANCODE_UNKNOWN)
                        ImGui::Text(locale::Text("%s can't be used here. Press another key; Esc or B cancels."), RefusedName(s_refused).c_str());
                    else
                        ImGui::Text(locale::Text("Press the key for %s. Esc or B cancels."), locale::Text(o.label));
                    PopAccentText();
                    ImGui::TextDisabled(" ");
                    return;
                }
                ImGui::TextWrapped("%s", locale::Text(o.description));
                // The keyboard's keys for what the legend names in
                // controller terms (Y, X); and a key on several controls is
                // fine (the defaults share the arrows), but say which, in
                // case it wasn't meant.
                std::string also = SharedWith(e);
                ImGui::TextDisabled(locale::Text("Enter sets the key, Backspace clears it, Delete restores the row.%s%s%s"),
                    also.empty() ? "" : "  Also: ", also.c_str(), also.empty() ? "" : ".");
                return;
            }
            ImGui::TextWrapped("%s", locale::Text(o.description));
            if ((o.id == Id::Scaling || o.id == Id::Sharpening || (o.id == Id::AntiAliasing && video::UsesMetalFx())) &&
                !video::UpscalingNote().empty())
            {
                ImGui::TextDisabled("%s", video::UpscalingNote().c_str());
                return;
            }
            if (o.id == Id::Fullscreen && video::FullscreenLocked())
            {
                ImGui::TextDisabled("%s", locale::Text("Always on in Game Mode: Steam sets the resolution (Properties > Game Resolution)."));
                return;
            }
            // The window's own fullscreen button (or the system) changed it for
            // this session; the setting still decides how the game starts.
            if (o.id == Id::Fullscreen && video::FullscreenWindowDiffers() && !settings::EnvOverride(o.id))
            {
                ImGui::TextDisabled(locale::Text("The window is %s for now. The game starts %s, as set here."),
                    video::WindowFullscreen() ? "fullscreen" : "windowed", settings::GetBool(Id::Fullscreen) ? "fullscreen" : "windowed");
                return;
            }
            // The experiment's finer scale wins over Screen Resolution's.
            if (float scale = video::PresentScaleOverride(); o.id == Id::PresentScale && scale > 0.0f)
                ImGui::TextDisabled(locale::Text("BURNOUT_PRESENT_SCALE sets %.2f for this run."), double(scale));
            else if (const char* env = settings::EnvOverride(o.id))
                ImGui::TextDisabled(locale::Text("%s controls this setting. Remove it to edit the saved choice: %s."), env,
                    settings::FormatValue(o.id, settings::GetStoredValue(o.id)).c_str());
            else if (settings::RestartPending(o.id))
            {
                PushAccentText();
                ImGui::TextUnformatted(locale::Text("Takes effect the next time the game starts."));
                PopAccentText();
            }
            else if (o.apply == settings::Apply::Restart)
                ImGui::TextDisabled("%s", locale::Text("Changes take effect the next time the game starts."));
            else if (std::string note = o.id == Id::FrameRate ? video::AutoFrameRateNote() : std::string(); !note.empty())
                ImGui::TextDisabled("%s", note.c_str());  // what Auto runs at now
            // The wheel the page is about - and, for the two rows that need
            // its motors, whether this system can drive them at all (macOS
            // has no driver for these wheels, so it cannot).
            else if (o.category == Category::Wheel)
            {
                const std::string name = hid::wheel::Name();
                if ((o.id == Id::ForceFeedback || o.id == Id::CenteringForce) && !hid::wheel::HasForceFeedback())
                    ImGui::TextDisabled(locale::Text("%s: this system can't drive its motors, so this does nothing. Steering, the pedals and "
                                        "the buttons all work."), name.empty() ? "This wheel" : name.c_str());
                else if (!name.empty())
                    ImGui::TextDisabled(locale::Text("%s is connected."), name.c_str());
                else
                    ImGui::TextDisabled(" ");
            }
            else
                ImGui::TextDisabled(" ");
        }
    }

    void DrawSettings()
    {
        float s = Scale();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 size(std::min(viewport->WorkSize.x - 2 * kSpaceL * s, 1040.0f * s), std::min(viewport->WorkSize.y - 2 * kSpaceL * s, 640.0f * s));
        ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
        // Keep the focus (a click or a touchscreen tap outside the panel
        // would leave the D-pad nothing to move).
        if (s_opened || !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
            ImGui::SetNextWindowFocus();
        if (s_opened)
        {
            s_opened = false;
            s_focusFirstRow = true;
        }
        // The game's look: square, the game dimmed behind a dark glass.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.028f, 0.025f, 0.72f));
        // The game behind dims, as under its own menus.
        ImGui::GetBackgroundDrawList()->AddRectFilled(viewport->Pos,
            ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), IM_COL32(10, 8, 6, 120));
        const bool shown = ImGui::Begin("Settings", nullptr, flags);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        if (!shown)
        {
            ImGui::End();
            return;
        }

#if defined(__APPLE__) && TARGET_OS_IOS
        // No F1 key to speak of: the controller's chord (its Back and Start
        // are View and Menu on an Xbox pad), or touch.
        TitleText("Settings", "View + Menu on a controller, or tap with three fingers");
#elif defined(__APPLE__)
        MenuTitle("F1  /  CMD+,  /  BACK + START");
#else
        TitleText("Settings", "F1 / Back + Start");
#endif

        // Pages: LB/RB (Q/E) or the tabs; not under a dialog, which keeps them.
        const bool dialog = ImGui::IsPopupOpen("reset") || UpdateDialogOpen() || QuitPromptOpen()
            || ImGui::IsPopupOpen("burnout-addons")
            || ImGui::IsPopupOpen("save-backups")
            ;
        if (int delta = dialog ? 0 : PageDelta())
        {
            Category next = s_category;
            do
                next = Category((int(next) + delta + kCategories) % kCategories);
            while (!PageShown(next));
            s_category = next;
            s_tabPending = true;
            s_focusFirstRow = true;
        }
        if (!PageShown(s_category))  // the keyboard, or the wheel, went away while on its page
        {
            s_category = Category::Input;
            s_tabPending = true;
            s_focusFirstRow = true;
        }
        // X on the Controls page is the rows' (KeyRow: the row's default
        // keys). To ImGui it is also the window-switching key: a press
        // starts the toggle and the release moves the cursor to the root
        // window. Owning the key while the page shows keeps that off; the
        // rows' IsKeyPressed, which accepts any owner, still sees it.
        if (s_category == Category::Controls)
            ImGui::SetKeyOwner(ImGuiKey_GamepadFaceLeft, ImHashStr("controls-x"));
        s_described = Id::Count;
        s_describedBugReport = false;
        s_describedUpdate = false;
        s_describedAddons = false;
        s_describedAbout = false;
        s_describedLanguages = false;
        s_rewindHelp=s_chordHelp=false;
        if (s_category!=Category::Rewind) s_chordRecording=false;
        s_describedSaveBackups = false;
        s_describedGamertag = false;
        s_describedServerAddress = false;
        s_describedCheck = false;
        s_describedMusicFolder = false;
        s_describedMadden = false;
        s_describedCal = false;
        if (s_waiting != Id::Count && settings::Info(s_waiting).category != s_category)
            EndWait();
        if (s_calibrating && s_category != Category::Wheel)
            CancelCalibrate();  // the page has gone (or the wheel has)
        // For the B check at the end: B's nav cancel (in ImGui's NewFrame,
        // before this) takes the waiting row's focus, and KeyRow then ends
        // the wait before that check could see it.
        const bool wasWaiting = s_waiting != Id::Count || s_chordRecording;
        // Tabs in the labels' face, the chosen one lit yellow.
        ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(1, 1, 1, 0.06f));
        ImGui::PushStyleColor(ImGuiCol_TabSelected, ImVec4(0.96f, 0.71f, 0.15f, 0.16f));
        const bool tabs = ImGui::BeginTabBar("pages", ImGuiTabBarFlags_NoTooltip | ImGuiTabBarFlags_FittingPolicyScroll);
        if (tabs)
        {
            for (int c = 0; c < kCategories; c++)
            {
                Category category = Category(c);
                if (!PageShown(category))
                    continue;
                ImGuiTabItemFlags tabFlags = (s_tabPending && category == s_category) ? ImGuiTabItemFlags_SetSelected : 0;
                ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
                ImGui::PushFont(burnout::LabelFont(), 22.0f);
                bool open = ImGui::BeginTabItem((Upper(settings::CategoryName(category)) + "###" + settings::CategoryName(category)).c_str(),
                    nullptr, tabFlags);
                ImGui::PopFont();
                ImGui::PopItemFlag();
                if (!open)
                    continue;
                if (!s_tabPending && category != s_category)
                {
                    s_category = category;  // clicked
                    s_focusFirstRow = true;
                }
                if (category == s_category)
                    s_tabPending = false;
                // Only once the page shown is the one chosen: the frame after
                // LB/RB still draws the old page.
                bool focusFirst = s_focusFirstRow && category == s_category;
                if (focusFirst)
                    s_focusFirstRow = false;

                float footer = ImGui::GetTextLineHeightWithSpacing() * 3.0f + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3;
                // A row's highlight reaches half an item spacing past the
                // row (Selectable pads itself to meet its neighbours), and
                // its focus outline a pixel or so further: so past the first
                // and last rows too, where the list's edge cut it. The list
                // keeps room for it, and starts that much higher so the rows
                // stay where they were.
                if (category == Category::Achievements)
                    DrawAchievementSummary();
                if (category==Category::Rewind)
                {
                    ImGui::PushFont(burnout::LabelFont(),kTextFontSize);
                    ImGui::TextWrapped("%s",RewindStateText().c_str());
                    ImGui::PopFont();
                }
                const ImVec2 cellPadding(kSpaceM * s, 2.0f * s);
                const float overhang = std::ceil(std::max(0.0f, ImGui::GetStyle().ItemSpacing.y * 0.5f - cellPadding.y + s));
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - overhang);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, overhang));
                bool rows = ImGui::BeginChild("rows", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
                ImGui::PopStyleVar();
                // The rows in the game's face; the focused one is marked by
                // RowLabel (yellow between hazard dashes), not ImGui's box.
                ImGui::PushFont(burnout::RowFont(), category == Category::Codes ? 21.f : kRowFontSize);
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.04f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.96f, 0.71f, 0.15f, 0.10f));
                ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
                if (rows && category == Category::Achievements)
                    DrawAchievements(focusFirst);
                if (rows && category != Category::Achievements)
                {
                    float valueWidth = 0.0f;
                    if (category == Category::Controls)
                        valueWidth = 2 * KeyCellWidth() + KeyCellGap();
                    else
                        for (const auto& o : settings::Options())
                            if (o.category == category && !o.hidden)
                                valueWidth = std::max(valueWidth, ValueWidth(o));
                    if (category == Category::Audio)
                        valueWidth = std::max(valueWidth, ImGui::CalcTextSize("9999 FILES").x);
                    if (category == Category::Online)
                        for (const char* text : { "Checking...", "Not set", "WWWWWWWWWWWW" })
                            valueWidth = std::max(valueWidth, ImGui::CalcTextSize(locale::Text(text)).x);
                    if (category == Category::Advanced)
                        for (const char* text : { "Saving...", "Checking...", "Restore pending", UpdateRowValue() })
                            valueWidth = std::max(valueWidth, ImGui::CalcTextSize(Upper(text).c_str()).x);
                    if (category == Category::Wheel)
                        for (const char* text : { "START", "17/17" })
                            valueWidth = std::max(valueWidth, ImGui::CalcTextSize(locale::Text(text)).x);
                    if (category==Category::Rewind) valueWidth=std::max(valueWidth,250.f*s);
                    if (category==Category::Codes)
                        for (const char* text : { "Unlocked", "Unlocking...", "Unlock", "Open World Tour" })
                            valueWidth=std::max(valueWidth,ImGui::CalcTextSize(Upper(text).c_str()).x);
                    valueWidth += 2 * kSpaceM * s;
                    float arrow = ImGui::GetFrameHeight();
                    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPadding);
                    if (ImGui::BeginTable("options", 2))
                    {
                        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, valueWidth + arrow * 2 + ValueInset());
                        if (category==Category::Codes) ModsSection("Cheat Codes");
                        if (category == Category::Controls)
                            for (const auto& e : hid::keymap::kEntries)
                            {
                                KeyRow(e, valueWidth, focusFirst);
                                focusFirst = false;
                            }
                        else
                            for (const auto& o : settings::Options())
                                if (o.category == category && !o.hidden)
                                {
                                    if (category==Category::Codes && o.id==Id::CrashLaunchMeter)
                                    {
                                        MaddenRow(valueWidth);
                                        ModsSection("Modifications");
                                    }
                                    if (o.type==settings::Type::Key)
                                    {
                                        const auto& entry=KeyEntry(o.id);
                                        if (o.id!=entry.id) continue;
                                        KeyRow(entry,valueWidth,focusFirst);
                                    }
                                    else OptionRow(o, valueWidth, focusFirst);
                                    focusFirst = false;
                                }
                        if (category==Category::Rewind) RewindRows(valueWidth);
                        if (category == Category::Audio)
                            MusicFolderRow(valueWidth);
                        if (category == Category::Wheel)
                            CalibrateRow(valueWidth);
                        if (category == Category::Online)
                            OnlineRows(valueWidth);
                        if (category == Category::Advanced)
                        {
                            LanguageResourcesRow(valueWidth);
                            BugReportRow(valueWidth);
                            std::string count = locale::Format("{} connected", game::BurnoutAddons().size());
                            if (ActionRow("burnout-addons", "Burnout Revenge Add-ons", count.c_str(), valueWidth, s_describedAddons))
                                s_openAddons = true;
                            if (ActionRow("save-backups", "Save Backups", SaveBackupsRowValue().c_str(), valueWidth, s_describedSaveBackups))
                                OpenSaveBackups();
                            // The version, a row like the others; its help
                            // line has the build and the legal notices.
                            const std::string version = Upper((std::string("v") + report::Version()).c_str());
                            ActionRow("about", "About", version.c_str(), valueWidth, s_describedAbout);
                        }
                        ImGui::EndTable();
                    }
                    ImGui::PopStyleVar();
                }
                ImGui::PopStyleColor(4);
                ImGui::PopFont();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopStyleColor(2);

        ImGui::Separator();
        // The game's description face (its menus' bold italic, in sentence case).
        ImGui::PushFont(burnout::LabelFont(), kTextFontSize);
        HelpLine();
        ImGui::PopFont();

        // Buttons (the help above runs to one, two or three lines), in the
        // labels' face.
        ImGui::PushFont(burnout::LabelFont(), 20.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ButtonRowAtBottom();
        bool anyChanged = false;
        // The Wheel page's pedal axes are hidden options, and Reset page
        // does put them back, so they count as a change of the page.
        const bool hiddenCounts = s_category == Category::Wheel || s_category==Category::Rewind;
        for (const auto& o : settings::Options())
            if (o.category == s_category && (!o.hidden || hiddenCounts) && !settings::IsDefault(o.id))
                anyChanged = true;
        ImGui::BeginDisabled(!anyChanged || s_category == Category::Achievements);
        if (ImGui::Button(locale::Text("RESET PAGE###Reset page")))
        {
            menu_sounds::Play(menu_sounds::Sound::Select);
            ImGui::OpenPopup("reset");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(locale::Text("CLOSE###Close")))
        {
            menu_sounds::Play(menu_sounds::Sound::Back);
            CloseSettings();
        }
#if BR_QUIT_GAME
        // Last, after Close: Down from the rows lands on the footer's first
        // button, so Quit Game takes a Right more, and then asks.
        ImGui::SameLine();
        if (ImGui::Button(locale::Text("QUIT GAME###Quit Game")) && s_quit.Choose() == quit::Outcome::Asking)
        {
            fprintf(stderr, "[ui] Quit Game: asking\n");
            ImGui::OpenPopup(kQuitPopup);
        }
#endif
        ImGui::PopStyleVar();
        ImGui::PopFont();
        ImGui::SameLine();
        // (The Controls page leaves out the D-pad: with Quit Game in the
        // footer its legend was wider than the window and lost Close.)
        if (s_category == Category::Controls)
            Legend({ { "Set key", "A" }, { "Clear", "Y" }, { "Default", "X" }, { "Page", "LB RB" }, { "Close", "B" } });
        else
            Legend({ { "Choose", "D-PAD" }, { "Page", "LB RB" }, { "Close", "B" } });

        if (s_openAddons) { ImGui::OpenPopup("burnout-addons"); s_openAddons = false; }
        // The dialogs (Reset, add-ons, updates, Quit Game) are the game's
        // prompts (burnout::PushDialogStyle); the update notes' text too.
        ImGui::PushFont(burnout::LabelFont(), kTextFontSize);
        bool popup = false;  // a dialog has the input: B is its, not the menu's
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        }
        burnout::PushDialogStyle();
        const bool resetShown = ImGui::BeginPopupModal("reset", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
        burnout::PopDialogStyle();
        if (resetShown)
        {
            burnout::DialogFrame();
            popup = true;
            const float width = burnout::kDialogWidth * s;
            burnout::DialogText(locale::Format("Reset every {} setting to its default?", locale::Text(settings::CategoryName(s_category))).c_str(), width);
            ImGui::Dummy(ImVec2(0, 10 * s));
            if (burnout::DialogOption("Reset", width))
            {
                for (const auto& o : settings::Options())
                    if (o.category == s_category && (!o.hidden || s_category==Category::Rewind))
                    {
                        double from = settings::GetStoredValue(o.id);
                        LogChange(o, from, settings::Reset(o.id));
                    }
                // The pedals' axes are hidden options: the loop above
                // leaves them, so the wheel puts them back itself.
                if (s_category == Category::Wheel)
                {
                    CancelCalibrate();
                    hid::wheel::ResetAxes();
                    fprintf(stderr, "[settings] menu: calibrate: back to the device's own axes\n");
                }
                ImGui::CloseCurrentPopup();
            }
            if (burnout::DialogOption("Cancel", width) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                ImGui::CloseCurrentPopup();
            ImGui::SetItemDefaultFocus();
            ImGui::Dummy(ImVec2(0, 6 * s));
            burnout::Hints({ { "Select", "A" }, { "Back", "B" } });
            burnout::EndDialog();
        }
        if (DrawUpdateDialog())
            popup = true;
        if (DrawAddons()) popup = true;
        if (DrawTextDialog(s)) popup = true;
        if (DrawSaveBackups()) popup = true;
#if BR_QUIT_GAME
        if (DrawQuitPrompt())
            popup = true;
#endif
        ImGui::PopFont();
        // B (or Esc) closes the menu; while a Controls row was waiting for
        // a key at the top of this frame it ends the wait instead (the
        // cursor goes with it, as after the Reset dialog: the next D-pad
        // press brings it back). The Esc key itself never gets here then:
        // SettingsMenuTakesKey took its press.
        if (!popup && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
            (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
        {
            menu_sounds::Play(menu_sounds::Sound::Back);
            if (wasWaiting)
            { EndWait();s_chordRecording=false; }
            else if (s_calibrating)
            {
                fprintf(stderr, "[settings] menu: calibrate: stopped at %s\n", hid::wheel::StepName(s_calStep));
                CancelCalibrate();
            }
            else
                CloseSettings();
        }
        // The cursor moved (D-pad, arrows, LB/RB): the game's click.
        static ImGuiID lastNav = 0;
        const ImGuiID nav = ImGui::GetCurrentContext()->NavId;
        if (nav && lastNav && nav != lastNav && !s_opened)
            menu_sounds::Play(menu_sounds::Sound::Move);
        lastNav = nav;
        ImGui::End();
    }

    void ResetSettingsMenu()
    {
        s_chordRecording=false;s_chordMessage.clear();
        s_opened = true;
        menu_sounds::Play(menu_sounds::Sound::Select);
        EndWait();  // a wait the menu was closed under is over
        CancelCalibrate();  // ...and so is a step it was asking for
        s_takenKey = SDL_SCANCODE_UNKNOWN;
        s_slotRow = Id::Count;
        s_slot = 0;
        // A dialog the menu was closed under (Start or Back alone closes it
        // from anywhere) doesn't come back with it.
        if (ImGuiContext* g = ImGui::GetCurrentContext(); g && g->OpenPopupStack.Size > 0)
            ImGui::ClosePopupToLevel(0, false);
        ForgetUpdateDialog();
    }

    bool SettingsMenuTakesKey(const SDL_KeyboardEvent& key)
    {
        if (s_chordRecording && key.down && key.scancode==SDL_SCANCODE_ESCAPE)
        { s_chordRecording=false;return true; }
        // The key a wait took or refused stays down a moment longer. ImGui never saw
        // it go down, so its OS repeats would read as fresh presses (Enter
        // would start a new wait, an arrow would move the cursor): they and
        // its release stay out of ImGui too.
        if (s_takenKey != SDL_SCANCODE_UNKNOWN && key.scancode == s_takenKey)
        {
            if (!key.down)
                s_takenKey = SDL_SCANCODE_UNKNOWN;
            if (!key.down || key.repeat)
                return true;
            s_takenKey = SDL_SCANCODE_UNKNOWN;  // pressed anew: its release went by unseen
        }
        if (s_waiting == Id::Count || !key.down || key.repeat)
            return false;
        const settings::Option& o = settings::Info(s_waiting);
        s_takenKey = key.scancode;
        if (key.scancode == SDL_SCANCODE_ESCAPE)
        {
            fprintf(stderr, "[settings] menu: %s.%s: no new key (Esc)\n", o.table, o.key);
            EndWait();
            return true;
        }
        // Not a key of the table (Cmd / Windows, F1, the media keys), or a
        // key pressed with Cmd / Windows held: the wait goes on and the help
        // line says so. (Cmd+, still closes the menu on a Mac: ProcessEvent's
        // toggle sees the comma before this does; the Cmd press before it
        // lands here and is refused.) Its repeats and release stay out of
        // ImGui like a taken key's: s_takenKey is already set.
        if (key.scancode == SDL_SCANCODE_UNKNOWN || !settings::keynames::Known(uint16_t(key.scancode)) || (key.mod & SDL_KMOD_GUI))
        {
            s_refused = key.scancode;
            fprintf(stderr, "[settings] menu: %s.%s: %s can't be used\n", o.table, o.key, RefusedName(key.scancode).c_str());
            return true;
        }
        double from = settings::GetStoredValue(o.id);
        LogChange(o, from, settings::Set(o.id, double(key.scancode)));
        EndWait();
        return true;
    }

    bool SettingsMenuRecordingChord() { return s_chordRecording; }

    bool ControllerShortcutsAllowed()
    {
        return Ready() && !HasModal() && s_waiting == Id::Count && !s_chordRecording && !s_calibrating &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    }

    bool RequestControllerQuit(SDL_JoystickID controller)
    {
#if BR_QUIT_GAME
        // Do not replace installation/notices, save dialogs, calibration or
        // a binding being recorded. A fresh press can ask after they close.
        if (!ControllerShortcutsAllowed())
            return false;
        if (s_quit.Choose() != quit::Outcome::Asking)
            return false;
        fprintf(stderr, "[ui] Quit Game: asking (Home/Guide/PS)\n");
        // BeginFrame copies the callback before invoking it, so its first-frame
        // state must survive those copies.
        const auto first = std::make_shared<bool>(true);
        SetModal([first] {
            if (*first) { ImGui::OpenPopup(kQuitPopup); *first = false; }
            ImGui::PushFont(burnout::LabelFont(), kTextFontSize);
            DrawQuitPrompt();
            ImGui::PopFont();
            if (!s_quit.Asking() && !s_quit.Quitting())
            {
                SetModal(nullptr);
                if (!SettingsOpen())
                {
                    XamNotifySystemUi(false);
                    apu::SetMenuDuck(false);
                }
            }
        }, true, controller);
        if (!SettingsOpen())
        {
            XamNotifySystemUi(true);
            apu::SetMenuDuck(true);
        }
        return true;
#else
        return false;
#endif
    }

    void ForgetQuitPrompt()
    {
#if BR_QUIT_GAME
        if (s_quit.Dismiss() == quit::Outcome::Cancelled)
            fprintf(stderr, "[ui] Quit Game: cancelled (the menu closed)\n");
#endif
    }
}
