// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The display gamma ramp a game programs into the Xbox 360's display
// controller: a 256-entry table (10 bits per channel) that the 360 applies to
// every pixel at scan-out, after the GPU is done with the frame. Direct3D
// writes it through the GPU's DC_LUT_* registers (PM4 type-0 register
// writes), from the ramp the game passes to SetGammaRamp, adjusted for the
// display by the kernel's VdGetCurrentDisplayGamma (gpu/imports_video.cpp).
// Most Wanted and Carbon pass a power curve from their own brightness value
// (Most Wanted's 360 menus never change it from the one it sets at boot).
//
// The command processor feeds every write of those registers to a Decoder
// (video/display_gamma.cpp keeps the one in use); a complete table is handed
// to the presenter, which applies it while the "Xbox 360 Display Gamma"
// setting is on, as it is by default (video/display_tone.h).
//
// Register protocol after Xenia (src/xenia/gpu/register_table.inc and
// CommandProcessor::WriteRegister, BSD license), which follows the M56
// (ATI/AMD R5xx) display controller's documentation:
//   DC_LUT_RW_MODE (0x1921)       bit 0: 0 = the 256-entry table, 1 = PWL
//   DC_LUT_RW_INDEX (0x1922)      the entry the next access goes to (bits 0:7);
//                                 a write also restarts DC_LUT_SEQ_COLOR at red
//   DC_LUT_SEQ_COLOR (0x1923)     one 10-bit component per write (bits 6:15):
//                                 red, green, blue, then the next entry
//   DC_LUT_PWL_DATA (0x1924)      a PWL segment (base 6:15, delta 22:31); three
//                                 writes (R, G, B) an entry, 128 entries
//   DC_LUT_30_COLOR (0x1925)      a whole entry: blue 0:9, green 10:19, red
//                                 20:29; then the index moves on by one
//   DC_LUT_WRITE_EN_MASK (0x1927) which components a write changes: bit 0
//                                 blue, 1 green, 2 red
//   DC_LUTA_CONTROL (0x1930)      0: the table; other values select PWL
// Direct3D writes RW_MODE 0, RW_INDEX 0, WRITE_EN_MASK 7, then for each entry
// DC_LUT_30_COLOR and RW_INDEX = the next index (a COND_WRITE once the color
// register reads back), ending with RW_INDEX = 0x100.
//
// Header only, no runtime dependencies (tests/display_tone_test.cpp).
#pragma once
#include <array>
#include <bitset>
#include <cstdint>

namespace video::display_gamma
{
    constexpr uint32_t kRegRwMode = 0x1921;
    constexpr uint32_t kRegRwIndex = 0x1922;
    constexpr uint32_t kRegSeqColor = 0x1923;
    constexpr uint32_t kRegPwlData = 0x1924;
    constexpr uint32_t kRegColor30 = 0x1925;
    constexpr uint32_t kRegWriteEnMask = 0x1927;
    constexpr uint32_t kRegLutaControl = 0x1930;

    // The registers the Decoder follows (the command processor's cheap test).
    constexpr bool IsLutRegister(uint32_t index)
    {
        return index - kRegRwMode <= kRegLutaControl - kRegRwMode;
    }

    // 256 entries of 10-bit red, green, blue: what the display shows for a
    // framebuffer value k is entry[k] / 1023.
    struct Table
    {
        std::array<std::array<uint16_t, 3>, 256> entry{};

        bool operator==(const Table&) const = default;

        // The ramp that changes nothing: k * 1023 / 255, rounded.
        static constexpr Table Identity()
        {
            Table t;
            for (uint32_t k = 0; k < 256; k++)
                t.entry[k] = { uint16_t((k * 1023 + 127) / 255), uint16_t((k * 1023 + 127) / 255), uint16_t((k * 1023 + 127) / 255) };
            return t;
        }
    };

    class Decoder
    {
    public:
        // One register write, in command-stream order. True when it completed
        // the table: every entry written since the last completion (Direct3D
        // rewrites all 256 each time). Writes outside the LUT registers are
        // ignored.
        bool Write(uint32_t index, uint32_t value)
        {
            switch (index)
            {
            case kRegRwMode:
                mode_ = value & 1;
                return false;
            case kRegRwIndex:
                index_ = value & 0xFF;
                component_ = 0;
                return false;
            case kRegWriteEnMask:
                mask_ = value & 7;
                return false;
            case kRegLutaControl:
                lutaControl_ = value;
                return false;
            case kRegColor30:
            {
                if (mode_ != 0)
                    return false;  // the PWL table is written through DC_LUT_PWL_DATA
                uint32_t k = index_;
                if (mask_ & 4) Set(k, 0, (value >> 20) & 0x3FF);
                if (mask_ & 2) Set(k, 1, (value >> 10) & 0x3FF);
                if (mask_ & 1) Set(k, 2, value & 0x3FF);
                if (mask_)
                    written_.set(k);
                component_ = 0;
                index_ = (index_ + 1) & 0xFF;
                return Completed();
            }
            case kRegSeqColor:
            {
                if (mode_ != 0)
                    return false;
                uint32_t k = index_;
                // Components in red, green, blue order; the mask's bits are
                // blue, green, red. Bits 0:5 are hardwired to zero.
                bool enabled = (mask_ >> (2 - component_)) & 1;
                if (enabled)
                {
                    Set(k, component_, (value >> 6) & 0x3FF);
                    seqWritten_[component_] = true;
                }
                if (++component_ == 3)
                {
                    component_ = 0;
                    if (seqWritten_[0] || seqWritten_[1] || seqWritten_[2])
                        written_.set(k);
                    seqWritten_ = {};
                    index_ = (index_ + 1) & 0xFF;
                    return Completed();
                }
                return false;
            }
            case kRegPwlData:
                pwlWrites_++;
                return false;
            default:
                return false;
            }
        }

        const Table& table() const { return table_; }
        // The table is what the display uses (not the PWL ramp).
        bool TableMode() const { return (lutaControl_ & 0xF) == 0; }
        // DC_LUT_PWL_DATA writes seen (a game that uses the PWL ramp: not
        // applied, logged).
        uint32_t PwlWrites() const { return pwlWrites_; }
        // Entries written since the last completed table.
        size_t Pending() const { return written_.count(); }

    private:
        void Set(uint32_t k, uint32_t c, uint32_t v) { table_.entry[k][c] = uint16_t(v); }

        bool Completed()
        {
            if (!written_.all())
                return false;
            written_.reset();
            return true;
        }

        Table table_ = Table::Identity();
        std::bitset<256> written_;
        std::array<bool, 3> seqWritten_{};
        uint32_t mode_ = 0, index_ = 0, component_ = 0, mask_ = 7, lutaControl_ = 0, pwlWrites_ = 0;
    };

    // The runtime's ramp (video/display_gamma.cpp). Command processor thread:
    // every write of a LUT register (IsLutRegister), in stream order; a
    // completed table that differs from the last is published and logged.
    void OnRegisterWrite(uint32_t index, uint32_t value);
    // Any thread: bumped by each published table; 0 while the game hasn't
    // written one.
    uint64_t Generation();
    // The last published table and its generation (0: none, `out` is then
    // the identity).
    uint64_t Latest(Table& out);
}
