#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

// Named FFT target curves, stored globally in the user's app data so they can
// be recalled in any project (like the target curves in Tonal Balance).
struct CurvePreset
{
    juce::String name;
    std::array<float, 512> curve {};
};

class CurvePresetStore
{
public:
    explicit CurvePresetStore (juce::File fileToUse = defaultFile())
        : file (std::move (fileToUse))
    {
        load();
    }

    static juce::File defaultFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("LudensScope")
            .getChildFile ("curve-presets.json");
    }

    const std::vector<CurvePreset>& getPresets() const noexcept { return presets; }

    bool save (const juce::String& name, const float* curve)
    {
        const auto trimmed = name.trim();

        if (trimmed.isEmpty() || curve == nullptr)
            return false;

        CurvePreset preset;
        preset.name = trimmed;
        std::copy (curve, curve + 512, preset.curve.begin());

        for (auto& existing : presets)
        {
            if (existing.name == trimmed)
            {
                existing = preset;
                write();
                return true;
            }
        }

        presets.push_back (std::move (preset));
        write();
        return true;
    }

    bool remove (const juce::String& name)
    {
        const auto before = presets.size();

        presets.erase (std::remove_if (presets.begin(), presets.end(),
                                       [&] (const CurvePreset& preset)
                                       {
                                           return preset.name == name;
                                       }),
                       presets.end());

        if (presets.size() == before)
            return false;

        write();
        return true;
    }

    const CurvePreset* find (const juce::String& name) const
    {
        for (const auto& preset : presets)
        {
            if (preset.name == name)
                return &preset;
        }

        return nullptr;
    }

private:
    void load()
    {
        presets.clear();

        if (! file.existsAsFile())
            return;

        const auto parsed = juce::JSON::parse (file.loadFileAsString());

        if (auto* array = parsed.getArray())
        {
            for (const auto& item : *array)
            {
                if (auto* curveArray = item["curve"].getArray())
                {
                    CurvePreset preset;
                    preset.name = item["name"].toString().trim();

                    if (preset.name.isEmpty())
                        continue;

                    for (int i = 0; i < 512 && i < curveArray->size(); ++i)
                        preset.curve[(size_t) i] = (float) (double) curveArray->getReference (i);

                    presets.push_back (std::move (preset));
                }
            }
        }
    }

    void write() const
    {
        file.getParentDirectory().createDirectory();

        juce::Array<juce::var> array;

        for (const auto& preset : presets)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("name", preset.name);

            juce::Array<juce::var> curve;

            for (const float value : preset.curve)
                curve.add (value);

            object->setProperty ("curve", curve);
            array.add (juce::var (object));
        }

        file.replaceWithText (juce::JSON::toString (juce::var (array), true));
    }

    juce::File file;
    std::vector<CurvePreset> presets;
};
