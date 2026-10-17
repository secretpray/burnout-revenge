// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See scale_budget.h.
#include "scale_budget.h"

namespace video
{
    namespace
    {
        // "Apple <family><generation>[ <tier>] GPU": family 'A' (iPhones, the
        // iPad mini and the base iPad) or 'M' (iPad Pro and Air), tier "Pro"
        // or empty. False for any other name.
        bool ParseAppleChip(std::string_view name, char& family, unsigned& generation, std::string_view& tier)
        {
            constexpr std::string_view kApple = "Apple ";
            if (!name.starts_with(kApple))
                return false;
            name.remove_prefix(kApple.size());
            if (name.empty() || (name[0] != 'A' && name[0] != 'M'))
                return false;
            family = name[0];
            name.remove_prefix(1);
            generation = 0;
            size_t digits = 0;
            while (digits < name.size() && name[digits] >= '0' && name[digits] <= '9' && digits < 4)
                generation = generation * 10 + unsigned(name[digits++] - '0');
            if (digits == 0 || (digits < name.size() && name[digits] != ' '))
                return false;
            name.remove_prefix(digits < name.size() ? digits + 1 : digits);
            // What's left: "GPU", or "<tier> GPU". MoltenVK names the device
            // after Metal's, which ends in " GPU"; accept a name without it too.
            constexpr std::string_view kGpu = "GPU";
            if (name.ends_with(kGpu))
                name.remove_suffix(kGpu.size());
            while (!name.empty() && name.back() == ' ')
                name.remove_suffix(1);
            tier = name;
            return true;
        }
    }

    ScaleBudget MobileScaleBudget(std::string_view gpuName)
    {
        char family = 0;
        unsigned generation = 0;
        std::string_view tier;
        if (!ParseAppleChip(gpuName, family, generation, tier))
            return { 1, "an unrecognised GPU keeps 1x1" };
        if (family == 'A' && generation == 20 && tier == "Pro")
        {
            // iPhone 18 Pro Max (2868x1320 screen), on a charger, scripted
            // fly-in and race: 60 fps at 1x1 with GPU to spare (race 7.7 ms a
            // frame), but iOS's thermal state went nominal -> fair at 75 s and
            // -> serious at 350 s (Frame Rate's Auto then dropped to 30 fps),
            // and was still serious 25 minutes later. More pixels only heat
            // it sooner: 1x1 until 2x2 is shown to stay out of serious.
            return { 1, "the A20 Pro heats to serious within 6 minutes at 1x1 on a charger" };
        }
        if (family == 'A' && generation <= 17)
        {
            // iPhone 15 Pro Max (A17 Pro), scripted race: GPU 15 ms a frame
            // at 1x1, 36-40 ms at 2x2.
            return { 1, "A17 Pro and older hold a race at 60 fps at 1x1 only" };
        }
        if (family == 'M')
        {
            // iPad Pro M2: 2x2 = 43-50 fps in a race. Newer M chips are not
            // measured in an iPad.
            return { 1, "iPads keep 1x1: the M2 makes 43-50 fps at 2x2" };
        }
        // An A-series chip newer than the A17 Pro but not measured (the
        // A18/A19, a plain A20, an A21...).
        return { 1, "a chip not measured yet keeps 1x1" };
    }
}
