#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

// Captures what the computer itself is playing: the standalone app can then
// meter Spotify, a browser or a player without a DAW in between.
//
// The callback runs on the capture thread with separate stereo float frames at
// the requested sample rate. Platforms differ: macOS uses ScreenCaptureKit and
// needs the "Screen & System Audio Recording" permission, Windows uses WASAPI
// loopback, Linux has no native route (JACK/PipeWire already exposes the system
// capture as a regular device).
class SystemAudioCapture
{
public:
    using Callback = std::function<void (const float* left, const float* right, int numFrames)>;

    SystemAudioCapture();
    ~SystemAudioCapture();

    // False when the platform has no supported capture backend at runtime.
    static bool isPlatformSupported();

    // Explains what is missing, for tooltips and error boxes.
    static juce::String platformNote();

    // Requests permission if the OS requires it. Returns false and fills
    // `error` when the user declines or the stream cannot start.
    bool start (double sampleRate, Callback callback, juce::String& error);

    void stop();
    bool isRunning() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SystemAudioCapture)
};
