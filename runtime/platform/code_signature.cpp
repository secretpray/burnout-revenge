// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See code_signature.h.
//
// Formats (Apple's <mach-o/loader.h>, <mach-o/fat.h> and the code signing
// blobs of xnu's osfmk/kern/cs_blobs.h), read field by field so this builds
// and tests anywhere:
//   - Fat header, big-endian: magic 0xCAFEBABE (32-bit offsets, 20-byte
//     entries) or 0xCAFEBABF (64-bit, 32-byte entries), then the entries:
//     cputype, cpusubtype, offset, size, align (, reserved).
//   - mach_header_64, the file's (little-endian on arm64): magic 0xFEEDFACF,
//     cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved;
//     then the load commands, each cmd and cmdsize first.
//     LC_CODE_SIGNATURE (0x1D) is a linkedit_data_command: dataoff and
//     datasize, file offsets within the slice.
//   - The embedded signature, big-endian: a SuperBlob (magic 0xFADE0CC0,
//     length, count, then count index entries of type and offset from the
//     SuperBlob's start). Slot 5 is the XML entitlements blob (magic
//     0xFADE7171), slot 7 the DER one (0xFADE7172): magic, length (the
//     blob's whole size, header included), then the bytes.
#include "code_signature.h"

#include <algorithm>
#include <cstring>
#include <vector>

#ifdef __APPLE__
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#endif

namespace platform::codesign
{
    namespace
    {
        constexpr uint32_t kFatMagic = 0xCAFEBABE, kFatMagic64 = 0xCAFEBABF;
        constexpr uint32_t kMachMagic64 = 0xFEEDFACF;
        constexpr uint32_t kCpuArm64 = 0x0100000C;
        constexpr uint32_t kLcCodeSignature = 0x1D;
        constexpr uint32_t kSuperBlobMagic = 0xFADE0CC0;
        constexpr uint32_t kEntitlementsMagic = 0xFADE7171, kDerEntitlementsMagic = 0xFADE7172;
        constexpr uint32_t kSlotEntitlements = 5, kSlotDerEntitlements = 7;
        constexpr size_t kMachHeaderSize = 32;

        bool Has(std::span<const uint8_t> b, uint64_t at, uint64_t n)
        {
            return at <= b.size() && n <= b.size() - at;
        }
        uint32_t Be32(std::span<const uint8_t> b, size_t at)
        {
            return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
        }
        uint64_t Be64(std::span<const uint8_t> b, size_t at)
        {
            return uint64_t(Be32(b, at)) << 32 | Be32(b, at + 4);
        }
        uint32_t Le32(std::span<const uint8_t> b, size_t at)
        {
            return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
        }
    }

    std::optional<Range> MachOSlice(std::span<const uint8_t> fileStart, uint64_t fileSize)
    {
        if (!Has(fileStart, 0, 8))
            return std::nullopt;
        if (Le32(fileStart, 0) == kMachMagic64)
            return Range{ 0, fileSize };
        uint32_t magic = Be32(fileStart, 0);
        if (magic != kFatMagic && magic != kFatMagic64)
            return std::nullopt;
        uint32_t count = Be32(fileStart, 4);
        size_t entry = magic == kFatMagic64 ? 32 : 20;
        if (count == 0 || count > 64 || !Has(fileStart, 8, uint64_t(count) * entry))
            return std::nullopt;
        std::optional<Range> chosen;
        for (uint32_t i = 0; i < count; i++)
        {
            size_t at = 8 + i * entry;
            Range r;
            if (magic == kFatMagic64)
                r = { Be64(fileStart, at + 8), Be64(fileStart, at + 16) };
            else
                r = { Be32(fileStart, at + 8), Be32(fileStart, at + 12) };
            if (r.offset > fileSize || r.size > fileSize - r.offset)
                continue;
            if (Be32(fileStart, at) == kCpuArm64)
                return r;
            if (count == 1)
                chosen = r;
        }
        return chosen;
    }

    std::optional<Range> SignatureRange(std::span<const uint8_t> sliceStart, uint64_t sliceOffset)
    {
        if (!Has(sliceStart, 0, kMachHeaderSize) || Le32(sliceStart, 0) != kMachMagic64)
            return std::nullopt;
        uint32_t ncmds = Le32(sliceStart, 16), sizeofcmds = Le32(sliceStart, 20);
        if (!Has(sliceStart, kMachHeaderSize, sizeofcmds))
            return std::nullopt;
        size_t at = kMachHeaderSize, end = kMachHeaderSize + sizeofcmds;
        for (uint32_t i = 0; i < ncmds; i++)
        {
            if (at + 8 > end)
                return std::nullopt;
            uint32_t cmd = Le32(sliceStart, at), cmdsize = Le32(sliceStart, at + 4);
            if (cmdsize < 8 || cmdsize > end - at)
                return std::nullopt;
            if (cmd == kLcCodeSignature)
            {
                if (cmdsize < 16)
                    return std::nullopt;
                uint32_t dataoff = Le32(sliceStart, at + 8), datasize = Le32(sliceStart, at + 12);
                if (datasize == 0)
                    return std::nullopt;
                return Range{ sliceOffset + dataoff, datasize };
            }
            at += cmdsize;
        }
        return std::nullopt;
    }

    std::optional<Entitlements> FromSuperBlob(std::span<const uint8_t> blob)
    {
        if (!Has(blob, 0, 12) || Be32(blob, 0) != kSuperBlobMagic)
            return std::nullopt;
        uint32_t length = Be32(blob, 4), count = Be32(blob, 8);
        if (length < 12 || length > blob.size())
            return std::nullopt;
        blob = blob.first(length);
        if (!Has(blob, 12, uint64_t(count) * 8))
            return std::nullopt;
        Entitlements der;
        for (uint32_t i = 0; i < count; i++)
        {
            uint32_t type = Be32(blob, 12 + i * 8), offset = Be32(blob, 16 + i * 8);
            if (type != kSlotEntitlements && type != kSlotDerEntitlements)
                continue;
            if (!Has(blob, offset, 8))
                return std::nullopt;
            uint32_t magic = Be32(blob, offset), size = Be32(blob, offset + 4);
            if (size < 8 || !Has(blob, offset, size))
                return std::nullopt;
            const char* bytes = reinterpret_cast<const char*>(blob.data() + offset + 8);
            if (type == kSlotEntitlements && magic == kEntitlementsMagic)
                return Entitlements{ true, std::string(bytes, size - 8), false };  // XML: the one to read
            if (type == kSlotDerEntitlements && magic == kDerEntitlementsMagic)
                der = Entitlements{ true, std::string(bytes, size - 8), true };
        }
        return der;  // DER only, or found == false: no entitlements at all
    }

    Value Lookup(const Entitlements& entitlements, std::string_view key)
    {
        if (!entitlements.found || key.empty())
            return Value::Absent;
        const std::string& t = entitlements.text;
        if (entitlements.der)
        {
            // UTF8String (tag 0x0C, short length) holding the key, then a
            // BOOLEAN (0x01, length 1, 0x00 false, anything else true).
            if (key.size() >= 0x80)
                return Value::Absent;
            std::string pattern;
            pattern += char(0x0C);
            pattern += char(key.size());
            pattern += key;
            for (size_t at = t.find(pattern); at != std::string::npos; at = t.find(pattern, at + 1))
            {
                size_t v = at + pattern.size();
                if (v + 3 <= t.size() && t[v] == 0x01 && t[v + 1] == 0x01)
                    return t[v + 2] != 0 ? Value::True : Value::False;
            }
            return Value::Absent;
        }
        std::string pattern = "<key>" + std::string(key) + "</key>";
        size_t at = t.find(pattern);
        if (at == std::string::npos)
            return Value::Absent;
        at += pattern.size();
        while (at < t.size() && (t[at] == ' ' || t[at] == '\t' || t[at] == '\r' || t[at] == '\n'))
            at++;
        std::string_view rest = std::string_view(t).substr(at);
        if (rest.starts_with("<true/>") || rest.starts_with("<true />"))
            return Value::True;
        if (rest.starts_with("<false/>") || rest.starts_with("<false />"))
            return Value::False;
        return Value::Absent;
    }

    std::optional<Entitlements> FromFile(std::span<const uint8_t> file)
    {
        std::optional<Range> slice = MachOSlice(file, file.size());
        if (!slice)
            return std::nullopt;
        std::optional<Range> sig = SignatureRange(file.subspan(slice->offset, slice->size), slice->offset);
        if (!sig || !Has(file, sig->offset, sig->size))
            return std::nullopt;
        return FromSuperBlob(file.subspan(sig->offset, sig->size));
    }

    std::optional<Entitlements> ReadOwn()
    {
#ifdef __APPLE__
        char path[4096];
        uint32_t pathSize = sizeof(path);
        if (_NSGetExecutablePath(path, &pathSize) != 0)
            return std::nullopt;
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            return std::nullopt;
        off_t fileSize = lseek(fd, 0, SEEK_END);
        auto read = [fd](uint64_t offset, uint64_t length) {
            std::vector<uint8_t> bytes(static_cast<size_t>(length));
            ssize_t got = length ? pread(fd, bytes.data(), bytes.size(), off_t(offset)) : 0;
            bytes.resize(got > 0 ? size_t(got) : 0);
            return bytes;
        };
        std::optional<Entitlements> result;
        if (fileSize > 0)
        {
            std::vector<uint8_t> start = read(0, std::min<uint64_t>(uint64_t(fileSize), 4096));
            if (std::optional<Range> slice = MachOSlice(start, uint64_t(fileSize)))
            {
                std::vector<uint8_t> header = read(slice->offset, kMachHeaderSize);
                if (header.size() == kMachHeaderSize && Le32(header, 0) == kMachMagic64)
                {
                    uint64_t commands = std::min<uint64_t>(Le32(header, 20), 16u << 20);
                    std::vector<uint8_t> head = read(slice->offset, kMachHeaderSize + commands);
                    std::optional<Range> sig = SignatureRange(head, slice->offset);
                    if (sig && sig->size <= (64u << 20))
                        result = FromSuperBlob(read(sig->offset, sig->size));
                }
            }
        }
        close(fd);
        return result;
#else
        return std::nullopt;
#endif
    }
}
