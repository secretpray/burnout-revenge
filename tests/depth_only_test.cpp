// Tests for gpu/depth_only.h: draws in depth-only EDRAM mode run no pixel
// shader (Xenos writes their rasterised depth and stencil only). Up to v0.1.2
// the renderer ran the pixel shader still bound, and in the sun's shadow pass
// that was the alpha-tested tree casters' one: with no texture coordinates
// from the cars' (and tunnel roofs') vertex shaders it read texel (0,0)'s
// alpha and killed every pixel, so cars cast no sun shadow and tunnels let
// the sun through. From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/depth_only_test.cpp -o build/depth_only_test
//   build/depth_only_test runtime/gpu/renderer.cpp
// The shadow pass is modelled with the order and the state the game's
// commands have, on a small depth map. With
// renderer.cpp's path (ctest passes it), Draw is also checked to use the
// rule: the empty pixel shader for depth-only draws, before its shader checks
// and its pipeline, and no alpha test for them.
#include <gpu/depth_only.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using xe::gpu::xenos::EdramMode;

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void TestRule()
{
    CHECK(gpu::RunsGuestPixelShader(EdramMode::kColorDepth), "colour+depth draws run their pixel shader");
    CHECK(!gpu::RunsGuestPixelShader(EdramMode::kDepthOnly), "depth-only draws run none");
    CHECK(gpu::RunsGuestPixelShader(EdramMode::kDepthOnly, true), "BURNOUT_DEPTH_ONLY_PS=1: the bound one, as up to v0.1.2");
    CHECK(gpu::RunsGuestPixelShader(EdramMode::kColorDepth, true), "the override changes depth-only draws only");
}

static void TestEmptyShader()
{
    const std::string g = gpu::kEmptyPixelShaderGlsl;
    CHECK(g.rfind("#version 460\n", 0) == 0, "a GLSL 4.60 shader: %s", g.c_str());
    CHECK(g.find("void main()") != std::string::npos, "has main: %s", g.c_str());
    for (const char* word : { "discard", "out ", "in ", "uniform", "layout", "buffer", "gl_FragDepth" })
        CHECK(g.find(word) == std::string::npos, "no '%s' (no kill, outputs, inputs or resources): %s", word, g.c_str());
}

// The sun's shadow pass on a 32x32 map, as the game draws it each frame:
// a depth-only clear, the alpha-tested world casters (colour+depth), the
// depth-only world occluders (a tunnel roof), the resolve to 03A8A000 (B),
// the depth-only car casters, the resolve to 0444E000 (A).
namespace model
{
    constexpr int kSize = 32;
    using Map = std::array<float, kSize * kSize>;

    // The tree casters' pixel shader (ps_7f7cf48a3edfed26): kill when the
    // texture's alpha at the interpolated coordinate is 0.2 or less (PS c0.x,
    // with c1.x = 1 to enable it). Its texture: alpha 0 at texel (0,0) (as
    // the one bound for the cars samples), 1 over the leaves.
    float Alpha(int u, int v) { return (u == 0 && v == 0) ? 0.0f : 1.0f; }
    bool Kills(bool hasTexcoord, int u, int v)
    {
        // No texture coordinate exported by the vertex shader: the input
        // register reads 0 (as the renderer, and Xenia, give it).
        float a = hasTexcoord ? Alpha(u, v) : Alpha(0, 0);
        return a <= 0.2f;
    }

    struct Draw
    {
        EdramMode mode;
        int x0, y0, x1, y1;
        float z;
        bool texcoord;     // the vertex shader exports one
        bool always;       // depth test ALWAYS (the clear), else LEQUAL
        bool treeShader;   // the pixel shader bound is the tree casters' (else the clear's, which kills nothing)
    };

    void Run(Map& depth, const Draw& d, bool guestOverride)
    {
        bool shaderRuns = gpu::RunsGuestPixelShader(d.mode, guestOverride);
        for (int y = d.y0; y < d.y1; y++)
            for (int x = d.x0; x < d.x1; x++)
            {
                if (shaderRuns && d.treeShader && Kills(d.texcoord, x + 1, y + 1))
                    continue;
                float& z = depth[y * kSize + x];
                if (d.always || d.z <= z)
                    z = d.z;
            }
    }

    struct Pass { Map b, a; };

    Pass ShadowPass(bool guestOverride)
    {
        Map depth;
        depth.fill(0.0f);  // whatever EDRAM held
        Run(depth, { EdramMode::kDepthOnly, 0, 0, kSize, kSize, 1.0f, false, true, false }, guestOverride);  // the clear
        Run(depth, { EdramMode::kColorDepth, 2, 2, 8, 8, 0.47f, true, false, true }, guestOverride);         // a tree
        Run(depth, { EdramMode::kDepthOnly, 12, 0, 20, 32, 0.50f, false, false, true }, guestOverride);      // a tunnel roof
        Pass p;
        p.b = depth;                                                                                         // resolve B
        Run(depth, { EdramMode::kDepthOnly, 24, 20, 28, 30, 0.52f, false, false, true }, guestOverride);     // a car
        p.a = depth;                                                                                  // resolve A
        return p;
    }

    int Count(const Map& m, int x0, int y0, int x1, int y1, float z)
    {
        int n = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                n += m[y * kSize + x] == z;
        return n;
    }
}

static void TestShadowPass()
{
    using namespace model;
    Pass p = ShadowPass(false);
    CHECK(Count(p.b, 0, 0, kSize, kSize, 1.0f) == kSize * kSize - 36 - 8 * 32, "B: far plane where no caster is (%d)",
        Count(p.b, 0, 0, kSize, kSize, 1.0f));
    CHECK(Count(p.b, 2, 2, 8, 8, 0.47f) == 36, "B holds the tree (%d of 36)", Count(p.b, 2, 2, 8, 8, 0.47f));
    CHECK(Count(p.b, 12, 0, 20, 32, 0.50f) == 256, "B holds the tunnel roof (%d of 256)", Count(p.b, 12, 0, 20, 32, 0.50f));
    CHECK(Count(p.a, 24, 20, 28, 30, 0.52f) == 40, "A holds the car (%d of 40)", Count(p.a, 24, 20, 28, 30, 0.52f));
    int differ = 0;
    for (int i = 0; i < kSize * kSize; i++)
        differ += p.a[i] != p.b[i];
    CHECK(differ == 40, "A is B plus the car, nothing else (%d texels differ)", differ);

    // Up to v0.1.2 (the bound pixel shader run depth-only): the tree only,
    // A == B: no car shadow, no tunnel shade. The model must show the bug.
    Pass old = ShadowPass(true);
    CHECK(Count(old.b, 2, 2, 8, 8, 0.47f) == 36, "v0.1.2 model: the tree (%d)", Count(old.b, 2, 2, 8, 8, 0.47f));
    CHECK(Count(old.b, 12, 0, 20, 32, 0.50f) == 0, "v0.1.2 model: no tunnel roof (%d)", Count(old.b, 12, 0, 20, 32, 0.50f));
    CHECK(old.a == old.b, "v0.1.2 model: A == B, no car");
}

// renderer.cpp's Draw, comments left out.
static std::string DrawBody(const std::string& source)
{
    std::string code;
    std::istringstream in(source);
    for (std::string line; std::getline(in, line);)
    {
        size_t c = line.find("//");
        code += (c == std::string::npos ? line : line.substr(0, c)) + "\n";
    }
    size_t start = code.find("void Draw(uint32_t initiatorValue");
    if (start == std::string::npos)
        return {};
    // To the next top-level function of the namespace (Draw's closing brace
    // is followed by `void Submit()`).
    size_t end = code.find("void Submit()", start);
    return code.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

static std::string Squash(const std::string& s)
{
    std::string r;
    for (char c : s)
        if (c != ' ' && c != '\n' && c != '\t')
            r += c;
    return r;
}

static void TestRenderer(const char* path)
{
    std::ifstream f(path);
    CHECK(f.good(), "cannot read %s", path);
    if (!f.good())
        return;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string source = ss.str();
    const std::string draw = Squash(DrawBody(source));
    CHECK(!draw.empty(), "Draw not found in %s", path);
    if (draw.empty())
        return;
    size_t swap = draw.find("if(!gpu::RunsGuestPixelShader(edramMode,s_depthOnlyGuestPs))s_ps=DepthOnlyPixelShader();");
    size_t restore = draw.find("~RestorePs(){s_ps=ps;}}restorePs{s_ps};");
    size_t loaded = draw.find("if(!s_vs||!s_ps)");
    size_t key = draw.find("key.ps=s_ps->hash;");
    CHECK(swap != std::string::npos, "Draw gives depth-only draws the empty pixel shader");
    CHECK(restore != std::string::npos && restore < swap, "Draw puts the guest's pixel shader back after the draw");
    CHECK(loaded != std::string::npos && swap < loaded, "the swap comes before the 'no shaders loaded' check");
    CHECK(key != std::string::npos && swap < key, "the swap comes before the pipeline key");
    CHECK(draw.find("alpha_func!=xenos::CompareFunction::kAlways&&gpu::RunsGuestPixelShader(edramMode,s_depthOnlyGuestPs);") !=
        std::string::npos, "no alpha test for depth-only draws");
    const std::string all = Squash(source);
    CHECK(all.find("slot->info.glsl=gpu::kEmptyPixelShaderGlsl;") != std::string::npos, "DepthOnlyPixelShader is the empty shader");
}

int main(int argc, char** argv)
{
    TestRule();
    TestEmptyShader();
    TestShadowPass();
    if (argc > 1)
        TestRenderer(argv[1]);
    else
        printf("(no renderer.cpp given: its checks skipped)\n");
    if (g_failures)
    {
        printf("depth_only_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("depth_only_test: all passed\n");
    return 0;
}
