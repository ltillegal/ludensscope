#pragma once

#include <vector>

struct GuideZone
{
    const char* name;
    float low;
    float high;
};

struct GuidePreset
{
    const char* name;
    std::vector<GuideZone> zones;
};

inline const std::vector<GuidePreset>& guidePresets()
{
    static const std::vector<GuidePreset> presets = {
        { "General", {
            { "SUB",   20.0f,    60.0f },
            { "KICK",  60.0f,   120.0f },
            { "BASS", 120.0f,   250.0f },
            { "SNARE",250.0f,   500.0f },
            { "VOX",  500.0f,  2000.0f },
            { "PRES",2000.0f,  6000.0f },
            { "AIR", 6000.0f, 20000.0f },
        } },
        { "EDM", {
            { "SUB",   20.0f,    50.0f },
            { "KICK",  50.0f,   110.0f },
            { "BASS", 110.0f,   250.0f },
            { "LOWMID",250.0f,  600.0f },
            { "LEAD", 600.0f,  3000.0f },
            { "HATS",3000.0f, 12000.0f },
            { "AIR",12000.0f, 20000.0f },
        } },
        { "Hip-Hop", {
            { "SUB",   20.0f,    50.0f },
            { "808",   50.0f,   100.0f },
            { "KICK", 100.0f,   180.0f },
            { "SNARE",180.0f,   500.0f },
            { "VOX",  500.0f,  3000.0f },
            { "HATS",5000.0f, 12000.0f },
            { "AIR",12000.0f, 20000.0f },
        } },
        { "Rock", {
            { "KICK",  50.0f,   100.0f },
            { "BASS", 100.0f,   250.0f },
            { "GTR",  250.0f,  2000.0f },
            { "VOX", 2000.0f,  5000.0f },
            { "CYMB",5000.0f, 12000.0f },
            { "AIR",12000.0f, 20000.0f },
        } },
        { "Acoustic", {
            { "LOW",   40.0f,   120.0f },
            { "BODY", 120.0f,   400.0f },
            { "MID",  400.0f,  2000.0f },
            { "PRES",2000.0f,  6000.0f },
            { "AIR", 6000.0f, 20000.0f },
        } },
    };

    return presets;
}
