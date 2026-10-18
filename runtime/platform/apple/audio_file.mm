// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See ../audio_file.h.
//
// ExtAudioFile converts on read to the client format set here (48 kHz,
// stereo, interleaved float), whatever the file's own. Tags come from
// AudioFile's info dictionary (ID3, iTunes atoms, Vorbis comments).
#include <platform/audio_file.h>

#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cctype>

namespace platform::audio_file
{
    namespace
    {
        constexpr double kRate = 48000;

        CFURLRef Url(const std::filesystem::path& path)
        {
            const std::string s = path.string();
            return CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(s.data()),
                CFIndex(s.size()), false);
        }

        class SystemReader final : public Reader
        {
        public:
            explicit SystemReader(ExtAudioFileRef file) : m_file(file) {}
            ~SystemReader() override { ExtAudioFileDispose(m_file); }

            bool Read(std::vector<float>& out, uint32_t frames) override
            {
                const size_t at = out.size();
                out.resize(at + size_t(frames) * 2);
                AudioBufferList list;
                list.mNumberBuffers = 1;
                list.mBuffers[0].mNumberChannels = 2;
                list.mBuffers[0].mDataByteSize = frames * 2 * sizeof(float);
                list.mBuffers[0].mData = out.data() + at;
                UInt32 got = frames;
                if (ExtAudioFileRead(m_file, &got, &list) != noErr)
                    got = 0;
                out.resize(at + size_t(got) * 2);
                return got > 0;
            }

        private:
            ExtAudioFileRef m_file;
        };

        std::string Utf8(CFStringRef s)
        {
            if (!s)
                return {};
            char buffer[512];
            return CFStringGetCString(s, buffer, sizeof(buffer), kCFStringEncodingUTF8) ? std::string(buffer) : std::string();
        }
    }

    std::unique_ptr<Reader> Open(const std::filesystem::path& path, std::string& error)
    {
        CFURLRef url = Url(path);
        ExtAudioFileRef file = nullptr;
        OSStatus status = url ? ExtAudioFileOpenURL(url, &file) : -1;
        if (url)
            CFRelease(url);
        if (status != noErr || !file)
        {
            error = "can't be opened as audio";
            return nullptr;
        }
        AudioStreamBasicDescription client{};
        client.mSampleRate = kRate;
        client.mFormatID = kAudioFormatLinearPCM;
        client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        client.mChannelsPerFrame = 2;
        client.mBitsPerChannel = 32;
        client.mFramesPerPacket = 1;
        client.mBytesPerFrame = client.mBytesPerPacket = 2 * sizeof(float);
        if (ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client) != noErr)
        {
            ExtAudioFileDispose(file);
            error = "can't be converted to 48 kHz stereo";
            return nullptr;
        }
        return std::make_unique<SystemReader>(file);
    }

    Tags ReadTags(const std::filesystem::path& path)
    {
        Tags tags;
        CFURLRef url = Url(path);
        AudioFileID file = nullptr;
        if (url && AudioFileOpenURL(url, kAudioFileReadPermission, 0, &file) == noErr)
        {
            CFDictionaryRef info = nullptr;
            UInt32 size = sizeof(info);
            if (AudioFileGetProperty(file, kAudioFilePropertyInfoDictionary, &size, &info) == noErr && info)
            {
                tags.title = Utf8(static_cast<CFStringRef>(CFDictionaryGetValue(info, CFSTR(kAFInfoDictionary_Title))));
                tags.artist = Utf8(static_cast<CFStringRef>(CFDictionaryGetValue(info, CFSTR(kAFInfoDictionary_Artist))));
                if (CFStringRef d = static_cast<CFStringRef>(CFDictionaryGetValue(info, CFSTR(kAFInfoDictionary_ApproximateDurationInSeconds))))
                    tags.seconds = CFStringGetDoubleValue(d);
                CFRelease(info);
            }
            AudioFileClose(file);
        }
        if (url)
            CFRelease(url);
        if (tags.title.empty())
            tags.title = path.stem().string();
        return tags;
    }

    bool IsMusicFile(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        for (const char* known : { ".mp3", ".m4a", ".aac", ".mp4", ".alac", ".flac", ".wav", ".aif", ".aiff", ".aifc", ".caf" })
            if (ext == known)
                return true;
        return false;
    }
}
