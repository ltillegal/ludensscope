#include "SystemAudioCapture.h"

// Fallback backend: Windows loopback and the Linux route (JACK/PipeWire
// capture ports) are not implemented yet, so the standalone reports honestly
// that system audio is unavailable instead of failing silently.

struct SystemAudioCapture::Impl
{
    bool running = false;
};

SystemAudioCapture::SystemAudioCapture() : impl (std::make_unique<Impl>()) {}

SystemAudioCapture::~SystemAudioCapture() = default;

bool SystemAudioCapture::isPlatformSupported()
{
    return false;
}

juce::String SystemAudioCapture::platformNote()
{
   #if JUCE_WINDOWS
    return "System audio capture is not wired up on Windows yet.";
   #elif JUCE_LINUX
    return "On Linux, use a PipeWire or JACK monitor source: "
           "run the standalone with the JACK backend and pick system:capture.";
   #else
    return "System audio capture is not available on this platform.";
   #endif
}

bool SystemAudioCapture::start (double, Callback, juce::String& error)
{
    error = platformNote();
    return false;
}

void SystemAudioCapture::stop()
{
    impl->running = false;
}

bool SystemAudioCapture::isRunning() const noexcept
{
    return impl->running;
}
