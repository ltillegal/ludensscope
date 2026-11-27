#include "SystemAudioCapture.h"

#if JUCE_MAC

#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>

#include <atomic>
#include <vector>

namespace
{
    // Trampoline so the Objective-C output object can reach the C++ callback
    // without knowing anything about std::function.
    using Deliver = void (*) (void*, const float*, const float*, int);

    void deliverAudio (void* context, const float* left, const float* right, int frames)
    {
        auto* callback = static_cast<SystemAudioCapture::Callback*> (context);

        if (callback != nullptr && *callback)
            (*callback) (left, right, frames);
    }
}

@interface LudensScopeSystemAudioOutput : NSObject <SCStreamOutput>
{
@public
    Deliver deliver;
    void* context;
    std::vector<float> leftScratch;
    std::vector<float> rightScratch;
}
@end

@implementation LudensScopeSystemAudioOutput

- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
    ofType:(SCStreamOutputType)type
{
    juce::ignoreUnused (stream);

    if (type != SCStreamOutputTypeAudio || deliver == nullptr || context == nullptr)
        return;

    CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription (sampleBuffer);

    if (format == nullptr)
        return;

    const AudioStreamBasicDescription* asbd =
        CMAudioFormatDescriptionGetStreamBasicDescription (format);

    if (asbd == nullptr || (asbd->mFormatFlags & kAudioFormatFlagIsFloat) == 0)
        return;

    const int frames = (int) CMSampleBufferGetNumSamples (sampleBuffer);

    if (frames <= 0)
        return;

    const bool interleaved = (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0;
    const int channels = (int) asbd->mChannelsPerFrame;

    // AudioBufferList has a trailing array; reserve room for two buffers.
    AudioBufferList* list =
        (AudioBufferList*) malloc (sizeof (AudioBufferList) + sizeof (AudioBuffer));

    if (list == nullptr)
        return;

    CMBlockBufferRef block = nullptr;

    const OSStatus status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer (
        sampleBuffer, nullptr, list, sizeof (AudioBufferList) + sizeof (AudioBuffer),
        nullptr, nullptr, kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, &block);

    if (status == noErr && list->mNumberBuffers > 0)
    {
        const float* left = nullptr;
        const float* right = nullptr;

        if (! interleaved && list->mNumberBuffers >= 2)
        {
            left  = (const float*) list->mBuffers[0].mData;
            right = (const float*) list->mBuffers[1].mData;
        }
        else if (channels >= 1)
        {
            // Interleaved: split into scratch buffers for the caller.
            const float* source = (const float*) list->mBuffers[0].mData;

            if (source != nullptr)
            {
                leftScratch.resize ((size_t) frames);
                rightScratch.resize ((size_t) frames);

                for (int i = 0; i < frames; ++i)
                {
                    leftScratch[(size_t) i] = source[i * channels];
                    rightScratch[(size_t) i] = channels > 1 ? source[i * channels + 1]
                                                            : source[i * channels];
                }

                left = leftScratch.data();
                right = rightScratch.data();
            }
        }

        if (left != nullptr)
            deliver (context, left, right != nullptr ? right : left, frames);
    }

    if (block != nullptr)
        CFRelease (block);

    free (list);
}

@end

struct SystemAudioCapture::Impl
{
    Callback callback;

    LudensScopeSystemAudioOutput* output = nil;
    SCStream* stream = nil;
    bool running = false;
};

SystemAudioCapture::SystemAudioCapture() : impl (std::make_unique<Impl>()) {}

SystemAudioCapture::~SystemAudioCapture()
{
    stop();
}

bool SystemAudioCapture::isPlatformSupported()
{
    if (@available (macOS 13.0, *))
        return true;

    return false;
}

juce::String SystemAudioCapture::platformNote()
{
    if (@available (macOS 13.0, *))
        return "System audio needs the \"Screen & System Audio Recording\" permission: "
               "System Settings -> Privacy & Security.";

    return "System audio capture needs macOS 13.0 or newer.";
}

bool SystemAudioCapture::start (double sampleRate, Callback callbackIn, juce::String& error)
{
    if (@available (macOS 13.0, *))
    {
        stop();

        impl->callback = std::move (callbackIn);

        auto* output = [[LudensScopeSystemAudioOutput alloc] init];
        output->context = &impl->callback;
        output->deliver = deliverAudio;

        __block SCShareableContent* content = nil;
        __block NSError* contentError = nil;
        __block NSError* startError = nil;
        __block BOOL started = NO;

        dispatch_semaphore_t semaphore = dispatch_semaphore_create (0);

        // This is the call that triggers the system permission prompt.
        [SCShareableContent getShareableContentExcludingDesktopWindows: NO
                                                   onScreenWindowsOnly: NO
                                                     completionHandler: ^(SCShareableContent* c,
                                                                          NSError* e)
        {
            content = c;
            contentError = e;
            dispatch_semaphore_signal (semaphore);
        }];

        if (dispatch_semaphore_wait (semaphore,
                                     dispatch_time (DISPATCH_TIME_NOW, 20ll * NSEC_PER_SEC)) != 0)
        {
            error = "Timed out waiting for system audio access.";
            return false;
        }

        if (content == nil || content.displays.count == 0)
        {
            error = contentError != nil ? juce::String::fromUTF8 (contentError.localizedDescription.UTF8String)
                                        : juce::String ("No display available for audio capture.");
            return false;
        }

        SCDisplay* display = content.displays.firstObject;

        if (display == nil)
        {
            error = "No display available for audio capture.";
            return false;
        }

        SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay: display
                                                        excludingWindows: @[]];

        SCStreamConfiguration* configuration = [[SCStreamConfiguration alloc] init];
        configuration.capturesAudio = YES;
        configuration.excludesCurrentProcessAudio = YES;
        configuration.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        configuration.channelCount = 2;
        configuration.showsCursor = NO;

        // Video is unavoidable with a display filter; keep it as cheap as
        // possible since only the audio is used.
        configuration.width = 2;
        configuration.height = 2;
        configuration.minimumFrameInterval = CMTimeMake (1, 1);

        SCStream* stream = [[SCStream alloc] initWithFilter: filter
                                              configuration: configuration
                                                   delegate: nil];

        dispatch_queue_t queue = dispatch_queue_create ("ludensscope.systemaudio",
                                                        DISPATCH_QUEUE_SERIAL);
        NSError* addError = nil;

        if (! [stream addStreamOutput: output
                                 type: SCStreamOutputTypeAudio
                   sampleHandlerQueue: queue
                                error: &addError])
        {
            error = addError != nil ? juce::String::fromUTF8 (addError.localizedDescription.UTF8String)
                                    : juce::String ("Could not attach the audio output.");
            return false;
        }

        semaphore = dispatch_semaphore_create (0);

        [stream startCaptureWithCompletionHandler: ^(NSError* e)
        {
            started = (e == nil);
            startError = e;
            dispatch_semaphore_signal (semaphore);
        }];

        if (dispatch_semaphore_wait (semaphore,
                                     dispatch_time (DISPATCH_TIME_NOW, 20ll * NSEC_PER_SEC)) != 0
            || ! started)
        {
            error = startError != nil
                        ? juce::String::fromUTF8 (startError.localizedDescription.UTF8String)
                        : juce::String ("Could not start system audio capture. Check "
                                        "System Settings -> Privacy & Security -> "
                                        "Screen & System Audio Recording.");
            return false;
        }

        impl->output = output;
        impl->stream = stream;
        impl->running = true;
        return true;
    }

    error = platformNote();
    return false;
}

void SystemAudioCapture::stop()
{
    if (impl == nullptr || ! impl->running)
        return;

    impl->running = false;

    if (@available (macOS 13.0, *))
    {
        if (impl->stream != nil)
        {
            dispatch_semaphore_t semaphore = dispatch_semaphore_create (0);

            [impl->stream stopCaptureWithCompletionHandler: ^(NSError*) { dispatch_semaphore_signal (semaphore); }];
            dispatch_semaphore_wait (semaphore,
                                     dispatch_time (DISPATCH_TIME_NOW, 5ll * NSEC_PER_SEC));
        }
    }

    impl->stream = nil;
    impl->output = nil;
    impl->callback = nullptr;
}

bool SystemAudioCapture::isRunning() const noexcept
{
    return impl != nullptr && impl->running;
}

#endif
