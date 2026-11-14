#pragma once

#include <juce_cryptography/juce_cryptography.h>

#include <atomic>
#include <thread>
#include <juce_gui_basics/juce_gui_basics.h>

// Fetches the latest GitHub release, compares it with the version the plugin
// was built as, and downloads the platform zip after verifying its SHA-256.
// The plugin never replaces its own binary (it is loaded by the host); the
// package is saved to ~/Downloads and the user installs it after quitting.

struct UpdateInfo
{
    bool checked = false;
    bool available = false;
    juce::String currentVersion;
    juce::String latestVersion;
    juce::String notes;
    juce::String assetName;
    juce::String downloadUrl;
    juce::String checksumUrl;
    juce::String releasePage;
    juce::String error;
};

inline juce::String readUrlTextWithTimeout (const juce::URL& url, int timeoutMs)
{
    juce::WebInputStream web (url, false);
    web.withConnectionTimeout (2000);
    web.withExtraHeaders ("User-Agent: LudensScope\r\n"
                          "Accept: application/vnd.github+json");

    if (! web.connect (nullptr))
        return {};

   #if JUCE_LINUX
    // curl's WebInputStream::cancel() is not safe from another thread on
    // Linux; the detached check thread may stay blocked, but the editor is
    // never affected.
    juce::ignoreUnused (timeoutMs);
    return web.readEntireStreamAsString();
   #else
    std::atomic<bool> finished { false };
    std::thread watchdog ([&web, &finished, timeoutMs]
    {
        const int steps = juce::jmax (1, timeoutMs / 100);

        for (int i = 0; i < steps; ++i)
        {
            if (finished.load())
                return;

            juce::Thread::sleep (100);
        }

        web.cancel();
    });

    auto text = web.readEntireStreamAsString();
    finished.store (true);
    watchdog.join();
    return text;
   #endif
}

inline bool isNewerVersion (const juce::String& candidate, const juce::String& current)
{
    const auto parse = [] (juce::String text)
    {
        text = text.trimCharactersAtStart ("vV");
        juce::StringArray parts;
        parts.addTokens (text, ".", "");
        return parts;
    };

    const auto a = parse (candidate);
    const auto b = parse (current);

    for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
    {
        const int av = i < a.size() ? a[i].getIntValue() : 0;
        const int bv = i < b.size() ? b[i].getIntValue() : 0;

        if (av != bv)
            return av > bv;
    }

    return false;
}

inline UpdateInfo performUpdateCheck()
{
    UpdateInfo info;
    info.checked = true;
   #if defined (JucePlugin_VersionString)
    info.currentVersion = juce::String (JucePlugin_VersionString);
   #else
    info.currentVersion = juce::String (LUDENSCOPE_VERSION_STRING);
   #endif

    const auto jsonText = readUrlTextWithTimeout (
        juce::URL ("https://api.github.com/repos/ltillegal/ludensscope/releases/latest"), 5000);

    if (jsonText.isEmpty())
    {
        info.error = "Could not reach GitHub. Check your internet connection.";
        return info;
    }

    const auto json = juce::JSON::parse (jsonText);

    if (! json.isObject() || json["tag_name"].toString().isEmpty())
    {
        info.error = "No releases published yet.";
        return info;
    }

    info.latestVersion = json["tag_name"].toString().trimCharactersAtStart ("vV");
    info.notes = json["body"].toString();
    info.releasePage = json["html_url"].toString();

   #if JUCE_MAC
    const juce::String platform = "macos";
   #elif JUCE_WINDOWS
    const juce::String platform = "windows";
   #else
    const juce::String platform = "linux";
   #endif

    if (auto* assets = json["assets"].getArray())
    {
        for (const auto& asset : *assets)
        {
            const auto name = asset["name"].toString();

            if (name == "SHA256SUMS.txt")
            {
                info.checksumUrl = asset["browser_download_url"].toString();
            }
            else if (name.containsIgnoreCase (platform) && name.endsWithIgnoreCase (".zip"))
            {
                info.assetName = name;
                info.downloadUrl = asset["browser_download_url"].toString();
            }
        }
    }

    info.available = isNewerVersion (info.latestVersion, info.currentVersion);

    if (info.available && (info.downloadUrl.isEmpty() || info.assetName.isEmpty()))
        info.error = "No update package for this platform in the latest release.";

    return info;
}

class UpdateDownloader : public juce::ThreadWithProgressWindow
{
public:
    UpdateDownloader (UpdateInfo updateToInstall, juce::File destination)
        : juce::ThreadWithProgressWindow ("Downloading LudensScope " + updateToInstall.latestVersion,
                                          true, true),
          update (std::move (updateToInstall)),
          dest (std::move (destination))
    {
        setStatusMessage ("Connecting...");
    }

    void run() override
    {
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs (5000)
                                 .withExtraHeaders ("User-Agent: LudensScope");

        auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile (update.assetName);
        temp.deleteFile();

        auto stream = juce::URL (update.downloadUrl).createInputStream (options);

        if (stream == nullptr)
        {
            message = "Could not download the update.";
            return;
        }

        juce::FileOutputStream out (temp);

        if (! out.openedOk())
        {
            message = "Could not write to " + temp.getFullPathName();
            return;
        }

        const auto total = stream->getTotalLength();
        juce::HeapBlock<char> buffer (32768);
        juce::int64 downloaded = 0;

        while (! threadShouldExit())
        {
            const int n = stream->read (buffer.getData(), 32768);

            if (n <= 0)
                break;

            out.write (buffer.getData(), (size_t) n);
            downloaded += n;

            if (total > 0)
                setProgress ((double) downloaded / (double) total);

            setStatusMessage (juce::String (downloaded / (1024 * 1024)) + " MB");
        }

        out.flush();

        if (threadShouldExit())
        {
            temp.deleteFile();
            message = "Cancelled.";
            return;
        }

        // The checksum lives next to the package in the same release.
        juce::String expected;

        if (update.checksumUrl.isNotEmpty())
        {
            const auto text = readUrlTextWithTimeout (juce::URL (update.checksumUrl), 5000);

            for (const auto& line : juce::StringArray::fromLines (text))
            {
                if (line.containsIgnoreCase (update.assetName))
                {
                    expected = line.upToFirstOccurrenceOf (" ", false, false).trim();
                    break;
                }
            }
        }

        if (expected.isEmpty())
        {
            temp.deleteFile();
            message = "The update package has no checksum; download aborted.";
            return;
        }

        juce::MemoryBlock data;

        if (! temp.loadFileAsData (data))
        {
            temp.deleteFile();
            message = "Could not read the downloaded file.";
            return;
        }

        if (! juce::SHA256 (data).toHexString().equalsIgnoreCase (expected))
        {
            temp.deleteFile();
            message = "Checksum mismatch; download aborted.";
            return;
        }

        dest.deleteFile();

        if (! temp.moveFileTo (dest))
        {
            message = "Could not save to " + dest.getFullPathName();
            return;
        }

        succeeded = true;
        message = dest.getFullPathName();
    }

    void threadComplete (bool userPressedCancel) override
    {
        juce::ignoreUnused (userPressedCancel);

        if (onFinished)
            onFinished (succeeded, message);

        delete this;
    }

    std::function<void (bool, juce::String)> onFinished;

    bool succeeded = false;
    juce::String message;

private:
    UpdateInfo update;
    juce::File dest;
};
