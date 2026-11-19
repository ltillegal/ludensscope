#pragma once

#include <cstring>
#include <juce_cryptography/juce_cryptography.h>

// Offline licence for LudensScope FULL.
//
// Key format:  LS1|<base64(payload)>|<base64(rsa-signature)>
// Payload:     LudensScope|FULL|<email>|<issued YYYY-MM-DD>
// Signature:   raw RSA (2048-bit) over the fixed 256-byte block
//              [0x01][SHA-256(payload) 32 bytes][zero padding]
//
// Only the seller holds the private key (scripts/make-licence, private key
// outside the repository). The plugin verifies locally with the public key
// below. No server, no account, no telemetry.
class LudensScopeLicence
{
public:
    static constexpr const char* kProduct = "LudensScope";

    // Seller public key in JUCE RSAKey toString() format: "<e hex>,<n hex>".
    // Replace only together with the private key used by scripts/make-licence.
    static constexpr const char* kPublicKey =
        "10001,9f2a28903a9f1d360a5abad2ddb1676fa9b48373171a8e188fdc149c3c66bcfa"
        "807ddb9902c448a35909bb107cb3118342236deb6bd67a8d73a58e561cbbd2ec353b03"
        "b8d1469ed243431949da5f1b4e7664e70598e121fb119a5abd7463babbd277a6a8f619"
        "a5836cefe16270b79d4e741d55f02abe86f125de477d0862428d06e01d377fa297e935"
        "a2671ffa0958ef8d7f18f1444a0f005b8144c5b8468c76d3f64a64049065941353daaf"
        "096c5390ed659b70040abc79c69c568ce92ef1dba23acce03350f239ba2532a2a533854"
        "16b5e2a7d14ae77bd29dc966fb483929d9f1849b30d7062f9e43d46dd9e31413c119f6"
        "47bc68bb0f3c309cb99852021e5";

    struct Info
    {
        bool valid = false;
        juce::String email, edition, issued, error;
    };

    //==============================================================================
    static Info parse (const juce::String& key,
                       const juce::String& publicKeyString = kPublicKey)
    {
        Info info;

        const auto parts = juce::StringArray::fromTokens (key.trim(), "|", "");
        if (parts.size() != 3 || parts[0] != "LS1")
        {
            info.error = "Not a LudensScope licence key.";
            return info;
        }

        const auto payloadBytes = decode (parts[1]);
        const auto payload = juce::String::fromUTF8 (
            static_cast<const char*> (payloadBytes.getData()),
            (int) payloadBytes.getSize());

        const auto fields = juce::StringArray::fromTokens (payload, "|", "");
        if (fields.size() != 4 || fields[0] != kProduct || fields[1] != "FULL")
        {
            info.error = "Licence payload is not a LudensScope FULL key.";
            return info;
        }

        if (fields[2].trim().isEmpty() || fields[3].trim().isEmpty())
        {
            info.error = "Licence payload is missing the email or issue date.";
            return info;
        }

        const auto signatureBytes = decode (parts[2]);
        if (signatureBytes.getSize() < 64)
        {
            info.error = "Licence signature is too short.";
            return info;
        }

        juce::BigInteger signature;
        signature.loadFromMemoryBlock (signatureBytes);

        juce::RSAKey publicKey (publicKeyString);
        if (! publicKey.isValid())
        {
            info.error = "Built-in licence public key is invalid.";
            return info;
        }

        if (! publicKey.applyToValue (signature))
        {
            info.error = "Licence signature could not be verified.";
            return info;
        }

        const auto expected = expectedBlock (payload);
        juce::BigInteger expectedValue;
        expectedValue.loadFromMemoryBlock (expected);

        if (signature != expectedValue)
        {
            info.error = "Licence signature does not match this key.";
            return info;
        }

        info.valid = true;
        info.edition = fields[1];
        info.email = fields[2].trim();
        info.issued = fields[3].trim();
        return info;
    }

    static bool verifyKey (const juce::String& key,
                           const juce::String& publicKeyString = kPublicKey)
    {
        return parse (key, publicKeyString).valid;
    }

    //==============================================================================
    static juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LudensScope")
                   .getChildFile ("licence.txt");
    }

    static juce::String load()
    {
        return file().loadFileAsString().trim();
    }

    static void save (const juce::String& key)
    {
        auto f = file();
        f.getParentDirectory().createDirectory();
        f.replaceWithText (key.trim());
        resetCache();
    }

    static bool saveAndVerify (const juce::String& key, juce::String& error)
    {
        const auto info = parse (key);
        if (! info.valid)
        {
            error = info.error;
            return false;
        }

        save (key);
        error.clear();
        return true;
    }

    //==============================================================================
    static bool isUnlocked()
    {
        if (testingUnlock)
            return true;

        if (cachedState < 0)
            refresh();

        return cachedState == 1 || cachedState == 2;
    }

    static bool isLegacy()
    {
        if (cachedState < 0)
            refresh();

        return cachedState == 2;
    }

    static bool isFullKey()
    {
        if (cachedState < 0)
            refresh();

        return cachedState == 1;
    }

    static juce::String statusText()
    {
        if (cachedState < 0)
            refresh();

        if (cachedState == 1)
            return "FULL";
        if (cachedState == 2)
            return "LEGACY FULL";

        return "LITE";
    }

    static juce::String licensedEmail()
    {
        if (cachedState < 0)
            refresh();

        return cachedEmail;
    }

    //==============================================================================
    // Test hooks: never used by the shipping UI.
    static void setTestingUnlock (bool shouldBeUnlocked)
    {
        testingUnlock = shouldBeUnlocked;
        cachedState = shouldBeUnlocked ? 1 : -1;
    }

    static void resetCache()
    {
        cachedState = -1;
        cachedEmail.clear();
    }

private:
    static juce::MemoryBlock decode (const juce::String& text)
    {
        juce::MemoryOutputStream out;
        juce::Base64::convertFromBase64 (out, text);
        return juce::MemoryBlock (out.getData(), out.getDataSize());
    }

    static juce::MemoryBlock expectedBlock (const juce::String& payload)
    {
        juce::MemoryBlock block (256, true);
        block[0] = 0x01;

        const auto hash = juce::SHA256 (payload.toRawUTF8(),
                                        payload.getNumBytesAsUTF8()).getRawData();
        const auto hashSize = juce::jmin ((size_t) hash.getSize(), (size_t) 32);
        std::memcpy (static_cast<char*> (block.getData()) + 1,
                     hash.getData(), hashSize);

        return block;
    }

    static bool legacyInstalled()
    {
        auto dir = file().getParentDirectory();
        if (! dir.isDirectory())
            return false;

        if (dir.getChildFile ("legacy.txt").existsAsFile())
            return true;

        if (dir.getChildFile ("licence.txt").existsAsFile())
            return false;

        if (dir.getChildFile ("theme.txt").existsAsFile()
            || dir.getChildFile ("curve-presets.json").existsAsFile())
        {
            dir.getChildFile ("legacy.txt")
               .replaceWithText ("Grandfathered FULL licence (0.4.x install)\n");
            return true;
        }

        return false;
    }

    static void refresh()
    {
        if (testingUnlock)
        {
            cachedState = 1;
            return;
        }

        cachedState = 0;
        cachedEmail.clear();

        const auto key = load();
        if (key.isNotEmpty())
        {
            const auto info = parse (key);
            if (info.valid)
            {
                cachedState = 1;
                cachedEmail = info.email;
                return;
            }
        }

        if (legacyInstalled())
            cachedState = 2;
    }

    static inline bool testingUnlock = false;
    static inline int cachedState = -1;
    static inline juce::String cachedEmail;
};
