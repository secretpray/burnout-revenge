// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include "my_music.h"

#ifndef BR_MY_MUSIC_TEST
#include "paths.h"
#endif

#include <algorithm>
#include <system_error>

namespace user::my_music
{
#ifndef BR_MY_MUSIC_TEST
    std::filesystem::path Folder()
    {
        return GetUserPath() / "Music";
    }
#endif

    std::vector<std::filesystem::path> Scan(const std::filesystem::path& folder,
        const std::function<bool(const std::filesystem::path&)>& isMusic)
    {
        std::vector<std::filesystem::path> files;
        std::error_code error;
        auto it = std::filesystem::recursive_directory_iterator(folder,
            std::filesystem::directory_options::skip_permission_denied, error);
        for (const auto end = std::filesystem::recursive_directory_iterator(); !error && it != end; it.increment(error))
        {
            const auto& path = it->path();
            if (path.filename().string().starts_with("."))
            {
                if (it->is_directory(error))
                    it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(error) && isMusic(path))
                files.push_back(path);
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    void Shuffle::SetFiles(std::vector<std::filesystem::path> files)
    {
        if (files == m_files)
            return;
        m_files = std::move(files);
        m_order.clear();
        m_position = 0;
    }

    std::filesystem::path Shuffle::Next()
    {
        if (m_files.empty())
            return {};
        if (m_position >= m_order.size())
        {
            m_order.resize(m_files.size());
            for (size_t i = 0; i < m_order.size(); i++)
                m_order[i] = i;
            std::shuffle(m_order.begin(), m_order.end(), m_random);
            // The round's first isn't the last one's last.
            if (m_order.size() > 1 && m_files[m_order[0]] == m_last)
                std::swap(m_order[0], m_order[1 + m_random() % (m_order.size() - 1)]);
            m_position = 0;
        }
        m_last = m_files[m_order[m_position++]];
        return m_last;
    }

    std::string DisplayName(const std::filesystem::path& file, const std::string& title, const std::string& artist)
    {
        if (!title.empty() && !artist.empty())
            return artist + " - " + title;
        if (!title.empty())
            return title;
        return file.stem().string();
    }
}
