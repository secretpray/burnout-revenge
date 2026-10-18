// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// My Music for the XMP player (user/my_music.h): Audio -> Music, the Music
// Folder's files through the system decoder, the toast that names a song.
#include "my_music.h"
#include "settings.h"
#include <platform/audio_file.h>
#include <ui/ui.h>

#include <mutex>

namespace user::my_music
{
    bool Chosen()
    {
        return settings::GetInt(settings::Id::Music) == int32_t(settings::MusicSource::MyMusic);
    }

    Pick Next()
    {
        static std::mutex mutex;
        static Shuffle shuffle{ std::random_device{}() };
        std::lock_guard lock(mutex);
        shuffle.SetFiles(Scan(Folder(), platform::audio_file::IsMusicFile));
        Pick pick{ .file = shuffle.Next(), .count = shuffle.Size() };
        if (!pick.file.empty())
        {
            const auto tags = platform::audio_file::ReadTags(pick.file);
            pick.name = DisplayName(pick.file, tags.title, tags.artist);
        }
        return pick;
    }

    void Announce(const std::string& text)
    {
        ui::Toast(text, 5.0);
    }

    void OnChoiceChanged(std::function<void()> onChange)
    {
        settings::Subscribe([onChange = std::move(onChange)](settings::Id id) {
            if (id == settings::Id::Music)
                onChange();
        });
    }
}
