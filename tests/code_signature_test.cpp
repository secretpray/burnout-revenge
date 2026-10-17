// Tests for platform/code_signature.h: the entitlements in a Mach-O's
// embedded code signature (the iOS app reads its own, kernel/memory_status.h),
// on Mach-O files built here byte by byte: thin and fat, XML and DER
// entitlements, an ad-hoc signature without any, unsigned, and every
// truncation and many corruptions of a good one (none may read out of
// bounds: build it once with -fsanitize=address too). From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/code_signature_test.cpp runtime/platform/code_signature.cpp -o build/code_signature_test
//   build/code_signature_test [a real executable, to print its entitlements]
#include <platform/code_signature.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace platform::codesign;

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    using Bytes = std::vector<uint8_t>;
    constexpr const char* kVa = "com.apple.developer.kernel.extended-virtual-addressing";
    constexpr const char* kMem = "com.apple.developer.kernel.increased-memory-limit";

    void Be32(Bytes& b, uint32_t v)
    {
        for (int s = 24; s >= 0; s -= 8)
            b.push_back(uint8_t(v >> s));
    }
    void Be64(Bytes& b, uint64_t v)
    {
        Be32(b, uint32_t(v >> 32));
        Be32(b, uint32_t(v));
    }
    void Le32(Bytes& b, uint32_t v)
    {
        for (int s = 0; s < 32; s += 8)
            b.push_back(uint8_t(v >> s));
    }
    void Append(Bytes& b, const std::string& s)
    {
        b.insert(b.end(), s.begin(), s.end());
    }

    Bytes Blob(uint32_t magic, const std::string& payload)
    {
        Bytes b;
        Be32(b, magic);
        Be32(b, uint32_t(8 + payload.size()));
        Append(b, payload);
        return b;
    }

    // A SuperBlob with these (slot, blob) entries, a CodeDirectory first as
    // a real signature has.
    Bytes SuperBlob(std::vector<std::pair<uint32_t, Bytes>> slots)
    {
        slots.insert(slots.begin(), { 0, Blob(0xFADE0C02, std::string(40, '\x11')) });
        Bytes header, body;
        uint32_t offset = uint32_t(12 + 8 * slots.size());
        Bytes index;
        for (auto& [type, blob] : slots)
        {
            Be32(index, type);
            Be32(index, offset + uint32_t(body.size()));
            body.insert(body.end(), blob.begin(), blob.end());
        }
        Be32(header, 0xFADE0CC0);
        Be32(header, uint32_t(12 + index.size() + body.size()));
        Be32(header, uint32_t(slots.size()));
        header.insert(header.end(), index.begin(), index.end());
        header.insert(header.end(), body.begin(), body.end());
        return header;
    }

    // A thin arm64 executable: a header, a stand-in segment command,
    // LC_CODE_SIGNATURE (unless `signature` is empty), some "code", then the
    // signature, 16-byte aligned as ld puts it.
    Bytes MachO(const Bytes& signature, uint32_t cpu = 0x0100000C)
    {
        bool sign = !signature.empty();
        uint32_t ncmds = sign ? 2 : 1, sizeofcmds = 72 + (sign ? 16 : 0);
        Bytes b;
        Le32(b, 0xFEEDFACF);
        Le32(b, cpu);
        Le32(b, 0);
        Le32(b, 2);  // MH_EXECUTE
        Le32(b, ncmds);
        Le32(b, sizeofcmds);
        Le32(b, 0);
        Le32(b, 0);
        Le32(b, 0x19);  // LC_SEGMENT_64, contents unread
        Le32(b, 72);
        b.resize(b.size() + 64, 0);
        uint32_t dataoff = 4096;
        if (sign)
        {
            Le32(b, 0x1D);
            Le32(b, 16);
            Le32(b, dataoff);
            Le32(b, uint32_t(signature.size()));
        }
        b.resize(dataoff, 0xCC);
        b.insert(b.end(), signature.begin(), signature.end());
        return b;
    }

    // A fat file of these slices (cpu type, contents), 32- or 64-bit headers.
    Bytes Fat(const std::vector<std::pair<uint32_t, Bytes>>& slices, bool fat64 = false)
    {
        Bytes b;
        Be32(b, fat64 ? 0xCAFEBABF : 0xCAFEBABE);
        Be32(b, uint32_t(slices.size()));
        uint64_t offset = 16384;
        std::vector<uint64_t> offsets;
        for (auto& [cpu, slice] : slices)
        {
            offsets.push_back(offset);
            Be32(b, cpu);
            Be32(b, 0);
            if (fat64)
            {
                Be64(b, offset);
                Be64(b, slice.size());
                Be32(b, 14);
                Be32(b, 0);
            }
            else
            {
                Be32(b, uint32_t(offset));
                Be32(b, uint32_t(slice.size()));
                Be32(b, 14);
            }
            offset += (slice.size() + 16383) / 16384 * 16384;
        }
        for (size_t i = 0; i < slices.size(); i++)
        {
            b.resize(offsets[i], 0);
            b.insert(b.end(), slices[i].second.begin(), slices[i].second.end());
        }
        return b;
    }

    std::string Xml(const std::string& body)
    {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
               "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n<plist version=\"1.0\">\n<dict>\n" + body + "</dict>\n</plist>\n";
    }

    // DER as codesign writes it: [APPLICATION 16] { INTEGER 1, [CONTEXT 16]
    // { SET of SEQUENCE { UTF8String key, value } } }, short lengths only.
    std::string Der(const std::vector<std::pair<std::string, int>>& entries)
    {
        auto tlv = [](uint8_t tag, const std::string& v) { return std::string(1, char(tag)) + char(v.size()) + v; };
        std::string set;
        for (auto& [key, value] : entries)
        {
            std::string val = value < 0 ? tlv(0x0C, "text") : tlv(0x01, std::string(1, char(value ? 0xFF : 0x00)));
            set += tlv(0x30, tlv(0x0C, key) + val);
        }
        return tlv(0x70, tlv(0x02, "\x01") + tlv(0xB0, tlv(0x31, set)));
    }

    const std::string kBoth = Xml(std::string("\t<key>application-identifier</key>\n\t<string>ABCDE12345.com.example.game</string>\n")
        + "\t<key>" + kVa + "</key>\n\t<true/>\n\t<key>" + kMem + "</key>\n\t<true/>\n");
}

int main(int argc, char** argv)
{
    // Thin, XML: both entitlements.
    {
        Bytes file = MachO(SuperBlob({ { 5, Blob(0xFADE7171, kBoth) } }));
        std::optional<Entitlements> e = FromFile(file);
        CHECK(e && e->found && !e->der, "thin XML read");
        CHECK(e && Lookup(*e, kVa) == Value::True, "extended VA true");
        CHECK(e && Lookup(*e, kMem) == Value::True, "memory limit true");
        CHECK(e && Lookup(*e, "com.apple.developer.kernel.extended-virtual") == Value::Absent, "a prefix isn't the key");
        CHECK(e && Lookup(*e, "get-task-allow") == Value::Absent, "absent key");
        CHECK(e && Lookup(*e, "application-identifier") == Value::Absent, "a string value isn't a boolean");
    }
    // XML: one false, one missing (what a tool that strips one would leave).
    {
        std::string xml = Xml(std::string("<key>") + kVa + "</key><false/>\n<key>get-task-allow</key>\n<true/>\n");
        std::optional<Entitlements> e = FromFile(MachO(SuperBlob({ { 5, Blob(0xFADE7171, xml) } })));
        CHECK(e && Lookup(*e, kVa) == Value::False, "false");
        CHECK(e && Lookup(*e, kMem) == Value::Absent, "missing");
        CHECK(e && Lookup(*e, "get-task-allow") == Value::True, "on its own line");
    }
    // Ad-hoc (the linker's): a CodeDirectory, no entitlements.
    {
        std::optional<Entitlements> e = FromFile(MachO(SuperBlob({})));
        CHECK(e && !e->found, "signed without entitlements");
        CHECK(e && Lookup(*e, kVa) == Value::Absent, "nothing to find");
    }
    // DER only.
    {
        std::string der = Der({ { "application-identifier", -1 }, { kVa, 1 }, { kMem, 0 } });
        std::optional<Entitlements> e = FromFile(MachO(SuperBlob({ { 7, Blob(0xFADE7172, der) } })));
        CHECK(e && e->found && e->der, "DER read");
        CHECK(e && Lookup(*e, kVa) == Value::True, "DER true");
        CHECK(e && Lookup(*e, kMem) == Value::False, "DER false");
        CHECK(e && Lookup(*e, "application-identifier") == Value::Absent, "DER string isn't a boolean");
        CHECK(e && Lookup(*e, "get-task-allow") == Value::Absent, "DER absent");
    }
    // Both: XML wins (whichever comes first in the index).
    {
        std::string der = Der({ { kVa, 0 } });
        for (bool derFirst : { false, true })
        {
            std::vector<std::pair<uint32_t, Bytes>> slots = { { 5, Blob(0xFADE7171, kBoth) }, { 7, Blob(0xFADE7172, der) } };
            if (derFirst)
                std::swap(slots[0], slots[1]);
            std::optional<Entitlements> e = FromFile(MachO(SuperBlob(slots)));
            CHECK(e && !e->der && Lookup(*e, kVa) == Value::True, "XML preferred (DER first: %d)", int(derFirst));
        }
    }
    // A blob of the wrong kind in the entitlements slot is skipped.
    {
        std::optional<Entitlements> e = FromFile(MachO(SuperBlob({ { 5, Blob(0xFADE0B01, kBoth) } })));
        CHECK(e && !e->found, "wrong magic in slot 5");
    }
    // Unsigned: no LC_CODE_SIGNATURE.
    CHECK(!FromFile(MachO({})), "unsigned");
    // Not a Mach-O at all.
    CHECK(!FromFile(Bytes(8192, 0x41)), "text");
    CHECK(!FromFile(Bytes{}), "empty");
    // 32-bit Mach-O magic isn't supported (iOS 11 dropped 32-bit).
    {
        Bytes file = MachO(SuperBlob({ { 5, Blob(0xFADE7171, kBoth) } }));
        file[0] = 0xCE;
        CHECK(!FromFile(file), "MH_MAGIC (32-bit)");
    }
    // Fat: the arm64 slice, wherever it is, with 32- and 64-bit headers.
    {
        Bytes arm = MachO(SuperBlob({ { 5, Blob(0xFADE7171, kBoth) } }));
        Bytes other = MachO(SuperBlob({}), 0x01000007);
        for (bool fat64 : { false, true })
        {
            std::optional<Entitlements> e = FromFile(Fat({ { 0x01000007, other }, { 0x0100000C, arm } }, fat64));
            CHECK(e && Lookup(*e, kVa) == Value::True, "fat, arm64 second (64-bit header: %d)", int(fat64));
            std::optional<Range> slice = MachOSlice(Fat({ { 0x0100000C, arm }, { 0x01000007, other } }, fat64), 1u << 20);
            CHECK(slice && slice->offset == 16384 && slice->size == arm.size(), "fat, arm64 first (64-bit header: %d)", int(fat64));
        }
        std::optional<Entitlements> only = FromFile(Fat({ { 0x0100000C, arm } }));
        CHECK(only && Lookup(*only, kMem) == Value::True, "fat with one slice");
        CHECK(!FromFile(Fat({ { 0x01000007, other }, { 0x01000012, other } })), "fat without arm64");
        Bytes bad = Fat({ { 0x0100000C, arm } });
        bad[8 + 8] = 0x7F;  // the slice's offset, far past the end
        CHECK(!FromFile(bad), "fat slice out of the file");
    }
    // Every truncation, and random corruption, of a good file: no crash, and
    // a truncation before the signature's end reads nothing.
    {
        Bytes good = MachO(SuperBlob({ { 5, Blob(0xFADE7171, kBoth) }, { 7, Blob(0xFADE7172, Der({ { kVa, 1 } })) } }));
        int read = 0;
        for (size_t n = 0; n < good.size(); n++)
        {
            Bytes cut(good.begin(), good.begin() + n);
            std::optional<Entitlements> e = FromFile(cut);
            read += e.has_value();
        }
        CHECK(read == 0, "truncated files read: %d", read);
        std::mt19937 rng(1234);
        int parsed = 0;
        for (int round = 0; round < 20000; round++)
        {
            Bytes b = good;
            int flips = 1 + int(rng() % 4);
            for (int f = 0; f < flips; f++)
            {
                // Mostly the structure (header, commands, the SuperBlob's
                // index and blob headers), sometimes anywhere.
                size_t at = rng() % 2 ? rng() % 120 : rng() % 3 ? 4096 + rng() % 48 : rng() % b.size();
                b[at % b.size()] = uint8_t(rng());
            }
            if (std::optional<Entitlements> e = FromFile(b))
            {
                parsed++;
                Lookup(*e, kVa);
                Lookup(*e, kMem);
            }
        }
        printf("  corrupted files: %d of 20000 still parsed (no crash)\n", parsed);
    }

    // A real executable (manual: the iOS app's, signed by Xcode or a tool).
    if (argc > 1)
    {
        std::ifstream in(argv[1], std::ios::binary);
        Bytes file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::optional<Entitlements> e = FromFile(file);
        auto name = [](Value v) { return v == Value::True ? "true" : v == Value::False ? "false" : "absent"; };
        if (!e)
            printf("%s: no signature read\n", argv[1]);
        else
            printf("%s: entitlements %s (%s); extended-virtual-addressing %s, increased-memory-limit %s\n", argv[1],
                e->found ? "found" : "none", e->der ? "DER" : "XML", name(Lookup(*e, kVa)), name(Lookup(*e, kMem)));
    }

    if (g_failures)
    {
        printf("code_signature_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("code_signature_test: all passed\n");
    return 0;
}
