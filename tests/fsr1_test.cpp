// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// Settings > Graphics > Upscaling's FSR 1: which pass draws a frame
// (video/upscale.h, on video::FitPicture's pictures), Sharpening as RCAS's
// strength, the shaders' constants against AMD's formulas, and their GLSL
// (video/fsr1.cpp): AMD's two headers compiled in byte for byte, one main
// in RCAS's (the tone variant renames it), and the band rules of
// video/band_filter.h in their code.
// Arguments: runtime/thirdparty/fsr1 (the vendored headers), runtime/video
// (presenter.cpp and fsr1.cpp read, and ../user/settings.cpp: FSR 1 isn't
// offered on iPhone and iPad, and no FSR 1 shader is compiled unless a
// frame's pass is FSR 1), and a folder the GLSL is written to
// (fsr1_glsl_check, when glslangValidator is installed, compiles it).
//
// Build by hand (from the repo root, after a CMake configure has written
// build/main/runtime/generated/fsr1_headers.inc):
//   c++ -std=c++20 -O2 -UNDEBUG -I runtime -I build/main/runtime/generated tests/fsr1_test.cpp \
//       runtime/video/fsr1.cpp runtime/video/picture_fit.cpp -o build/fsr1_test
//   build/fsr1_test runtime/thirdparty/fsr1 runtime/video build/fsr1_glsl
#include <user/settings.h>
#include <video/fsr1.h>
#include <video/picture_fit.h>
#include <video/upscale.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    int s_failures = 0;

#define CHECK(cond, ...)                                                    \
    do                                                                      \
    {                                                                       \
        if (!(cond))                                                        \
        {                                                                   \
            s_failures++;                                                   \
            fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
            fprintf(stderr, __VA_ARGS__);                                   \
            fprintf(stderr, "\n");                                          \
        }                                                                   \
    } while (0)

    using video::upscale::Choose;
    using video::upscale::Mode;
    using video::upscale::Pass;

    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    float Float(uint32_t bits)
    {
        float f;
        memcpy(&f, &bits, 4);
        return f;
    }

    size_t Count(const std::string& text, const std::string& what)
    {
        size_t n = 0;
        for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
            n++;
        return n;
    }

    // The pass for a 1280x720-shaped frame of imageW x imageH on a screen,
    // where the presenter puts it (Aspect Ratio Auto, the hooks present).
    Pass PassOn(Mode mode, uint32_t imageW, uint32_t imageH, uint32_t screenW, uint32_t screenH, int32_t* dw = nullptr, int32_t* dh = nullptr)
    {
        const video::PictureFit fit = video::FitPicture(screenW, screenH, 1280.0f, 720.0f, true, true);
        if (dw)
            *dw = fit.width, *dh = fit.height;
        return Choose(int32_t(mode), imageW, imageH, fit.width, fit.height);
    }

    void PassSelection()
    {
        static_assert(int32_t(Mode::Bicubic) == int32_t(settings::ScalingMode::Bicubic));
        static_assert(int32_t(Mode::Linear) == int32_t(settings::ScalingMode::Linear));
        static_assert(int32_t(Mode::Fsr1) == int32_t(settings::ScalingMode::Fsr1));
        struct Case
        {
            const char* what;
            uint32_t imageW, imageH, screenW, screenH;
            Pass fsr;  // with Upscaling on FSR 1
        };
        const Case cases[] = {
            { "1x on 1080p", 1280, 720, 1920, 1080, Pass::Fsr1 },
            { "1x on 1440p", 1280, 720, 2560, 1440, Pass::Fsr1 },
            { "1x on a MacBook Pro 14 (Vert+)", 1280, 720, 3024, 1898, Pass::Fsr1 },
            { "1.5x on 4K", 1920, 1080, 3840, 2160, Pass::Fsr1 },
            { "1x on the Steam Deck (720 rows stretched to 800)", 1280, 720, 1280, 800, Pass::Fsr1 },
            { "2x on a MacBook Pro 14 (Auto on an M1 Pro)", 2560, 1440, 3024, 1898, Pass::Fsr1 },
            { "1x on an ultrawide (Hor+)", 1280, 720, 3440, 1440, Pass::Fsr1 },
            { "1x in a 1280x720 window (1:1)", 1280, 720, 1280, 720, Pass::Bicubic },
            { "2x on 1080p (shrunk)", 2560, 1440, 1920, 1080, Pass::Bicubic },
            { "2x on 1440p (1:1)", 2560, 1440, 2560, 1440, Pass::Bicubic },
            { "3x2 on an ultrawide (columns shrunk)", 3840, 1440, 3440, 1440, Pass::Bicubic },
            { "1x in a small window (shrunk)", 1280, 720, 960, 540, Pass::Bicubic },
            { "1x2 on 1080p (columns enlarged, rows shrunk)", 1280, 1440, 1920, 1080, Pass::Bicubic },
        };
        for (const Case& c : cases)
        {
            int32_t dw = 0, dh = 0;
            const Pass fsr = PassOn(Mode::Fsr1, c.imageW, c.imageH, c.screenW, c.screenH, &dw, &dh);
            CHECK(fsr == c.fsr, "%s (%ux%u -> %dx%d): FSR 1 setting gives %s, want %s", c.what, c.imageW, c.imageH, dw, dh,
                video::upscale::PassName(fsr), video::upscale::PassName(c.fsr));
            // The other settings draw as they always did, enlarged or not.
            CHECK(PassOn(Mode::Bicubic, c.imageW, c.imageH, c.screenW, c.screenH) == Pass::Bicubic, "%s: bicubic", c.what);
            CHECK(PassOn(Mode::Linear, c.imageW, c.imageH, c.screenW, c.screenH) == Pass::Linear, "%s: bilinear", c.what);
            printf("  %-50s %4ux%-4u -> %4dx%-4d  %s\n", c.what, c.imageW, c.imageH, dw, dh, video::upscale::PassName(fsr));
        }
        // No picture, or a value the menu never stores: bicubic.
        CHECK(Choose(int32_t(Mode::Fsr1), 0, 0, 1920, 1080) == Pass::Bicubic, "no frame");
        CHECK(Choose(int32_t(Mode::Fsr1), 1280, 720, 0, 0) == Pass::Bicubic, "no picture");
        CHECK(Choose(7, 1280, 720, 1920, 1080) == Pass::Bicubic, "an unknown mode");
        // Enlarged along one axis by a single pixel counts; along neither doesn't.
        CHECK(video::upscale::Enlarged(1280, 720, 1281, 720) && !video::upscale::Enlarged(1280, 720, 1280, 720), "one pixel");
    }

    void Sharpness()
    {
        using video::upscale::RcasAmount;
        using video::upscale::RcasStops;
        CHECK(RcasAmount(0.0f) == 0.0f && std::isinf(RcasStops(0.0f)), "0 turns RCAS off");
        CHECK(RcasAmount(-1.0f) == 0.0f && RcasAmount(NAN) == 0.0f, "below 0, NaN: off");
        CHECK(RcasAmount(1.0f) == 1.0f && RcasStops(1.0f) == 0.0f, "1 is RCAS's maximum (0 stops)");
        CHECK(RcasAmount(2.0f) == 1.0f, "above 1: the maximum");
        CHECK(std::fabs(RcasStops(0.5f) - 1.0f) < 1e-6f, "0.5: one stop below");
        float last = -1.0f;
        for (int i = 0; i <= 20; i++)
        {
            const float a = RcasAmount(float(i) / 20.0f);
            CHECK(a > last || (i == 0 && a == 0.0f), "monotonic at %d/20", i);
            last = a;
        }
        // What RCAS gets: FsrRcasCon's con.x is the linear factor 2^-stops.
        for (float s : { 0.05f, 0.3f, 0.5f, 1.0f })
        {
            const auto con = video::fsr1::RcasConstants(RcasStops(s));
            CHECK(std::fabs(Float(con[0]) - RcasAmount(s)) < 1e-5f, "Sharpening %.2f: con.x %.6f, want %.6f", s, Float(con[0]), RcasAmount(s));
        }
        const auto off = video::fsr1::RcasConstants(RcasStops(0.0f));
        CHECK(off[0] == 0 && off[1] == 0, "off: con 0 (the shader copies EASU's picture)");
    }

    void EasuConstants()
    {
        // FsrEasuCon's formulas (ffx_fsr1.h), for the image's whole size.
        const std::array<uint32_t, 4> sizes[] = { { 1280, 720, 1920, 1080 }, { 1280, 720, 1280, 800 }, { 1920, 1080, 3840, 2160 },
            { 1280, 720, 3024, 1898 } };
        for (auto [sw, sh, dw, dh] : sizes)
        {
            const auto c = video::fsr1::EasuConstants(sw, sh, dw, dh);
            const float x = float(sw) / float(dw), y = float(sh) / float(dh);
            CHECK(std::fabs(Float(c[0]) - x) < 1e-6f && std::fabs(Float(c[1]) - y) < 1e-6f, "%ux%u -> %ux%u: con0.xy", sw, sh, dw, dh);
            CHECK(std::fabs(Float(c[2]) - (0.5f * x - 0.5f)) < 1e-6f && std::fabs(Float(c[3]) - (0.5f * y - 0.5f)) < 1e-6f, "con0.zw");
            CHECK(Float(c[4]) == 1.0f / float(sw) && Float(c[5]) == 1.0f / float(sh), "con1.xy");
            CHECK(Float(c[6]) == 1.0f / float(sw) && Float(c[7]) == -1.0f / float(sh), "con1.zw");
            CHECK(Float(c[8]) == -1.0f / float(sw) && Float(c[9]) == 2.0f / float(sh), "con2.xy");
            CHECK(Float(c[10]) == 1.0f / float(sw) && Float(c[11]) == 2.0f / float(sh), "con2.zw");
            CHECK(Float(c[12]) == 0.0f && Float(c[13]) == 4.0f / float(sh) && c[14] == 0 && c[15] == 0, "con3");
        }
    }

    void Glsl(const std::filesystem::path& fsrDir, const std::filesystem::path& videoDir, const std::filesystem::path& out)
    {
        const std::string easu = video::fsr1::EasuGlsl(), rcas = video::fsr1::RcasGlsl();
        // AMD's headers inside, byte for byte as vendored (unmodified).
        size_t halfInHeaders = 0;
        for (const char* name : { "ffx_a.h", "ffx_fsr1.h" })
        {
            const std::string header = Read(fsrDir / name);
            CHECK(header.size() > 10000, "%s read (%zu bytes)", name, header.size());
            CHECK(easu.find(header) != std::string::npos && rcas.find(header) != std::string::npos, "%s compiled in unchanged", name);
            halfInHeaders += Count(header, "#define A_HALF");  // their usage examples, in comments
        }
        CHECK(Count(easu, "#define FSR_EASU_F 1") == 1 && Count(rcas, "#define FSR_RCAS_F 1") == 1, "the 32-bit paths");
        CHECK(Count(easu, "#define A_HALF") == halfInHeaders && Count(rcas, "#define A_HALF") == halfInHeaders,
            "no 16-bit path of ours (MoltenVK, Intel): %zu, %zu, %zu in the headers' comments", Count(easu, "#define A_HALF"),
            Count(rcas, "#define A_HALF"), halfInHeaders);
        CHECK(Count(rcas, "void main()") == 1, "RCAS has one main (the tone variant renames it)");
        // band_filter.h's rules, as the GLSL writes them.
        CHECK(easu.find("vec2 pos = (vec2(ip) + 0.5) * vec2(p.srcSize) / vec2(p.outSize);") != std::string::npos, "EASU: SourceOf");
        CHECK(easu.find("return t < float(lo) ? ivec2(0, lo) : t < float(hi) ? ivec2(lo, hi) : ivec2(hi, size);") != std::string::npos, "EASU: SpanOf");
        CHECK(easu.find("texelFetch(src, clamp(t, spanLo, spanHi), 0)") != std::string::npos, "EASU: ClampTexel");
        CHECK(easu.find("p.bandOn == 0u ? textureGather(src, q, 0) : quadIn(q, 0)") != std::string::npos, "EASU: gathers without a band");
        CHECK(rcas.find("float t = (float(i) + 0.5) * float(srcSize) / float(outSize);") != std::string::npos, "RCAS: SourceOf");
        CHECK(rcas.find("return t < float(lo) ? 0 : t < float(hi) ? 1 : 2;") != std::string::npos, "RCAS: SpanIndex");
        CHECK(Count(rcas, "!= centreSpan.") == 2 && rcas.find("t.x = centre.x;") != std::string::npos && rcas.find("t.y = centre.y;") != std::string::npos,
            "RCAS: RcasNeighbour on both axes");
        // The presenter: the pass from Choose, RCAS's tone variant from ToneVariant, EASU before the swapchain's pass.
        const std::string presenter = Read(videoDir / "presenter.cpp");
        CHECK(presenter.find("upscale::Choose(settings::GetInt(settings::Id::Scaling), w, h, dw, dh)") != std::string::npos, "ChoosePass");
        CHECK(presenter.find("ToneVariant(fsr1::RcasGlsl().c_str())") != std::string::npos, "RCAS's tone variant");
        const size_t easuAt = presenter.find("RecordEasu(cmd, scaleInput, w, h, dw, dh, band, bandOn);");
        const size_t passAt = presenter.find("color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // black bars");
        CHECK(easuAt != std::string::npos && passAt != std::string::npos && easuAt < passAt, "EASU before the swapchain image's pass");
        // No FSR 1 shader or pipeline unless a frame's pass is FSR 1: the
        // GLSL is compiled only in EnsureFsr and RcasPipeline, and every
        // EnsureFsr() call is behind "pass == upscale::Pass::Fsr1".
        const size_t ensureAt = presenter.find("bool EnsureFsr()"), imageAt = presenter.find("void EnsureFsrImage(");
        CHECK(ensureAt != std::string::npos && imageAt != std::string::npos && ensureAt < imageAt, "EnsureFsr, RcasPipeline, EnsureFsrImage in order");
        size_t glslAt = 0, glslCount = 0;
        while ((glslAt = presenter.find("fsr1::", glslAt)) != std::string::npos)
        {
            if (presenter.compare(glslAt, 15, "fsr1::EasuGlsl(") == 0 || presenter.compare(glslAt, 15, "fsr1::RcasGlsl(") == 0)
            {
                glslCount++;
                CHECK(glslAt > ensureAt && glslAt < imageAt, "FSR 1 GLSL compiled outside EnsureFsr/RcasPipeline (offset %zu)", glslAt);
            }
            glslAt += 6;
        }
        CHECK(glslCount == 3, "FSR 1 GLSL compiled in 3 places (EASU, RCAS, RCAS's tone variant): %zu", glslCount);
        size_t calls = 0;
        for (size_t callAt = presenter.find("EnsureFsr()"); callAt != std::string::npos; callAt = presenter.find("EnsureFsr()", callAt + 11))
        {
            if (callAt == ensureAt + 5)
                continue;  // the definition
            calls++;
            const size_t lineAt = presenter.rfind('\n', callAt) + 1;
            CHECK(presenter.substr(lineAt, callAt - lineAt).find("pass == upscale::Pass::Fsr1 && ") != std::string::npos,
                "EnsureFsr() called without the FSR 1 pass: %s", presenter.substr(lineAt, presenter.find('\n', callAt) - lineAt).c_str());
        }
        CHECK(calls == 2, "EnsureFsr() called from ChoosePass and BURNOUT_UPSCALE_COMPARE only: %zu", calls);
        // iPhone and iPad: no FSR 1 in the menu (user/settings.cpp: its entry
        // compiled out there, so "fsr1" reads as unknown = bicubic).
        const std::string settingsCpp = Read(videoDir / ".." / "user" / "settings.cpp");
        const size_t entryAt = settingsCpp.find("{ int32_t(ScalingMode::Fsr1), \"fsr1\", \"FSR 1\" },");
        const size_t ifAt = settingsCpp.rfind("#if !(defined(__APPLE__) && TARGET_OS_IOS)", entryAt);
        const size_t endifAt = settingsCpp.find("#endif", ifAt == std::string::npos ? 0 : ifAt);
        CHECK(entryAt != std::string::npos && ifAt != std::string::npos && endifAt > entryAt
                && settingsCpp.substr(ifAt, entryAt - ifAt).find("#endif") == std::string::npos,
            "the FSR 1 Upscaling entry is compiled out on iPhone and iPad");
        // For fsr1_glsl_check: the shaders as glslang gets them (the tone
        // variant as ToneVariant makes it).
        std::filesystem::create_directories(out);
        std::ofstream(out / "easu.comp") << easu;
        std::ofstream(out / "rcas.frag") << rcas;
        std::string tone = rcas;
        tone.replace(tone.find("void main()"), 11, "void scaleMain()");
        tone += "\nlayout(set = 0, binding = 1) uniform sampler2D toneTable;\nvoid main()\n{\n    scaleMain();\n"
                "    color = vec4(textureLod(toneTable, vec2(color.r, 0.5), 0.0).r, color.gb, color.a);\n}\n";
        std::ofstream(out / "rcas_tone.frag") << tone;
    }
}

int main(int argc, char** argv)
{
    printf("the pass with Upscaling on FSR 1:\n");
    PassSelection();
    Sharpness();
    EasuConstants();
    if (argc >= 4)
        Glsl(argv[1], argv[2], argv[3]);
    else
        printf("(no folders given: the GLSL checks skipped)\n");
    if (s_failures)
    {
        printf("%d check(s) FAILED\n", s_failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
