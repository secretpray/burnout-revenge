// Tests for video::MobileScaleBudget (runtime/video/scale_budget.cpp): Auto
// internal resolution's budget on iPhones and iPads, chip by chip, and what
// it makes of the phones' screens with video::AutoRenderScale. From the repo
// root:
//   clang++ -std=c++20 -O2 -Iruntime tests/scale_budget_test.cpp runtime/video/scale_budget.cpp runtime/video/picture_fit.cpp -o build/scale_budget_test
//   build/scale_budget_test
#include <video/picture_fit.h>
#include <video/scale_budget.h>

#include <cstdio>
#include <cstring>

using video::AutoRenderScale;
using video::MobileScaleBudget;
using video::ScaleBudget;

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void Budget(const char* gpu, unsigned want)
{
    ScaleBudget b = MobileScaleBudget(gpu);
    printf("\"%s\": %u (%s)\n", gpu, b.pixels, b.reason);
    CHECK(b.pixels == want, "\"%s\": budget %u, want %u", gpu, b.pixels, want);
    CHECK(b.reason && std::strlen(b.reason) > 0, "\"%s\": no reason for the log", gpu);
}

// The scale Auto picks for a phone filling its screen (Hor+: the picture is
// the whole swapchain) with the budget its GPU gets.
static void Phone(const char* gpu, unsigned w, unsigned h, unsigned wantX, unsigned wantY)
{
    unsigned sx = 0, sy = 0;
    AutoRenderScale(w, h, MobileScaleBudget(gpu).pixels, sx, sy);
    printf("\"%s\" on %ux%u: %ux%u\n", gpu, w, h, sx, sy);
    CHECK(sx == wantX && sy == wantY, "\"%s\" on %ux%u: %ux%u, want %ux%u", gpu, w, h, sx, sy, wantX, wantY);
}

int main()
{
    printf("Measured chips:\n");
    Budget("Apple A20 Pro GPU", 1);  // iPhone 18 Pro Max: serious thermal state at 1x1 within 6 minutes
    Budget("Apple A20 Pro", 1);      // the same chip without Metal's " GPU"
    Budget("Apple A17 Pro GPU", 1);  // iPhone 15 Pro Max: 2x2 is 36-40 ms of GPU a frame
    Budget("Apple A16 GPU", 1);
    Budget("Apple A15 GPU", 1);
    Budget("Apple M2 GPU", 1);       // iPad Pro M2: 43-50 fps at 2x2

    printf("Chips not measured (1x1 until they are):\n");
    Budget("Apple A20 GPU", 1);       // the iPhone 18's A20, not the Pro
    Budget("Apple A18 Pro GPU", 1);
    Budget("Apple A19 Pro GPU", 1);
    Budget("Apple A21 Pro GPU", 1);   // a newer chip: not "A20 or later"
    Budget("Apple A22 Pro GPU", 1);
    Budget("Apple M4 GPU", 1);
    Budget("Apple M5 Pro GPU", 1);

    printf("Names that only look alike:\n");
    Budget("Apple A2 Pro GPU", 1);       // "A2", not "A20"
    Budget("Apple A200 Pro GPU", 1);
    Budget("Apple A20 Pro Max GPU", 1);  // another tier
    Budget("Apple A20 Promax GPU", 1);
    Budget("Apple A20X Pro GPU", 1);
    Budget("Apple A Pro GPU", 1);
    Budget("Apple GPU", 1);
    Budget("Apple ", 1);
    Budget("", 1);
    Budget("AMD Radeon RX 7600 (RADV NAVI33)", 1);
    Budget("A20 Pro GPU", 1);            // not Metal's name
    Budget("Apple A20 Pro GPU (simulated)", 1);

    printf("The phones' screens:\n");
    Phone("Apple A20 Pro GPU", 2868, 1320, 1, 1);  // iPhone 18 Pro Max: 1280x720 (2x2 would be 2560x1440)
    Phone("Apple A17 Pro GPU", 2796, 1290, 1, 1);  // iPhone 15 Pro Max: 1280x720
    Phone("Apple A20 GPU", 2622, 1206, 1, 1);
    // Present Scale 75% (a smaller drawable).
    Phone("Apple A20 Pro GPU", 2151, 990, 1, 1);

    if (g_failures)
    {
        printf("%d FAILED\n", g_failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
