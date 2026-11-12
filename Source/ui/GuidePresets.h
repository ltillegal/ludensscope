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
        { "Pop", {
            { "SUB",   20.0f,    60.0f },
            { "KICK",  60.0f,   120.0f },
            { "BASS", 120.0f,   260.0f },
            { "SNARE",260.0f,   600.0f },
            { "VOX",  600.0f,  3000.0f },
            { "PRES",3000.0f,  8000.0f },
            { "AIR", 8000.0f, 20000.0f },
        } },
        { "Metal", {
            { "KICK",  50.0f,   100.0f },
            { "BASS", 100.0f,   250.0f },
            { "GTR",  250.0f,  2000.0f },
            { "VOX", 2000.0f,  5000.0f },
            { "CYMB",5000.0f, 12000.0f },
            { "AIR",12000.0f, 20000.0f },
        } },
        { "Jazz", {
            { "LOW",   40.0f,   120.0f },
            { "BASS",  80.0f,   250.0f },
            { "PIANO",250.0f,  1500.0f },
            { "HORN", 500.0f,  2500.0f },
            { "BRUSH",2500.0f, 6000.0f },
            { "AIR", 6000.0f, 20000.0f },
        } },
        { "Orchestral", {
            { "LOW",   30.0f,    80.0f },
            { "CELLO", 80.0f,   250.0f },
            { "WOOD", 250.0f,  1000.0f },
            { "STRINGS",1000.0f,4000.0f },
            { "PRES",4000.0f, 10000.0f },
            { "AIR",10000.0f, 20000.0f },
        } },
        { "Lo-Fi", {
            { "SUB",   20.0f,    60.0f },
            { "KICK",  60.0f,   120.0f },
            { "BASS", 100.0f,   250.0f },
            { "MID",  250.0f,  1000.0f },
            { "VOX",  800.0f,  3000.0f },
            { "DUST",3000.0f,  8000.0f },
            { "AIR", 8000.0f, 16000.0f },
        } },
        { "R&B", {
            { "SUB",   20.0f,    50.0f },
            { "808",   50.0f,   100.0f },
            { "KICK",  80.0f,   150.0f },
            { "SNARE",150.0f,   500.0f },
            { "VOX",  500.0f,  2500.0f },
            { "KEYS",1000.0f,  4000.0f },
            { "AIR", 6000.0f, 20000.0f },
        } },
    };

    return presets;
}
