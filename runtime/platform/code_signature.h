// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The entitlements in the game's own code signature, as installed. On iOS
// what the app may do is what its signature says (and its provisioning
// profile allows: iOS won't install or open an app signed with entitlements
// its profile lacks), and a sideloading tool (AltStore, SideStore,
// Sideloadly...) re-signs the app with the player's own Apple Account: a
// tool may keep, or drop, the entitlements the app asks for
// (scripts/ios/BurnoutRevenge/BurnoutRevenge.entitlements). So the signature on
// the device answers "did this copy get Extended Virtual Addressing?" for
// the log and the startup message (kernel/memory_status.h).
//
// Parsing is pure (tests/code_signature_test.cpp): a Mach-O file (thin, or
// fat with an arm64 slice) -> its LC_CODE_SIGNATURE -> the embedded
// signature's SuperBlob -> the entitlements blob, as XML (slot 5) or DER
// (slot 7). ReadOwn() reads the running executable (Apple only).
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace platform::codesign
{
    // A file range: where the embedded signature is in the executable.
    struct Range
    {
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    // The arm64 slice of a fat file, or the whole file if thin: from the
    // file's first bytes (the fat header, which is small). nullopt: neither a
    // Mach-O nor a fat file with an arm64 (or the only) slice.
    std::optional<Range> MachOSlice(std::span<const uint8_t> fileStart, uint64_t fileSize);

    // The embedded signature's range in the file, from the slice's first
    // bytes (its header and load commands, `sliceOffset` into the file).
    // nullopt: not a 64-bit Mach-O, malformed, or unsigned (no
    // LC_CODE_SIGNATURE).
    std::optional<Range> SignatureRange(std::span<const uint8_t> sliceStart, uint64_t sliceOffset);

    struct Entitlements
    {
        bool found = false;  // the signature has an entitlements blob (else: none at all)
        std::string text;    // XML (slot 5); else the DER blob's bytes (slot 7)
        bool der = false;
    };
    // The entitlements from the embedded signature (a CS SuperBlob,
    // big-endian). nullopt: not a SuperBlob or malformed. found == false: a
    // signature without entitlements (an ad-hoc one, as the linker makes).
    std::optional<Entitlements> FromSuperBlob(std::span<const uint8_t> blob);

    enum class Value : uint8_t { Absent, False, True };
    // `key`'s boolean in the entitlements: XML <key>key</key><true/>, or DER
    // (UTF8String key, BOOLEAN). Absent: not there (or not a boolean).
    Value Lookup(const Entitlements& entitlements, std::string_view key);

    // The whole chain on a file's bytes (tests): nullopt as above.
    std::optional<Entitlements> FromFile(std::span<const uint8_t> file);

    // Apple: the running executable's entitlements, read from its file.
    // nullopt: unreadable, unsigned or not parsed (and elsewhere).
    std::optional<Entitlements> ReadOwn();
}
