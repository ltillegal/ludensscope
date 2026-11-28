#pragma once

#include <cstring>
#include <juce_cryptography/juce_cryptography.h>

// Offline licence for LudensScope PRO.
//
// Key format:  LS1|<base64(payload)>|<base64(rsa-signature)>
// Payload:     LudensScope|FULL|<email>|<issued YYYY-MM-DD>
//              FULL stays in the signed payload for compatibility with
//              existing keys; the UI calls this edition PRO.
// Signature:   raw RSA (2048-bit) over the fixed 256-byte block
//              [0x01][SHA-256(payload) 32 bytes][zero padding]
//
// Only the seller holds the private key (scripts/make-licence, private key
// outside the repository). The plugin verifies locally with the public key
// below. No server, no account, no telemetry.
//
// Machine activation: keys issued after kActivationCutoffDate are activated
// locally on up to kMaxMachines machines. The activation record is a plain
// sidecar file next to licence.txt and holds hashes of machine identifiers,
// never personal data. Older keys stay unrestricted so an update cannot take
// PRO away from an existing customer.
class LudensScopeLicence
{
public:
    // Each product carries its own tag inside the signed payload and its own
    // config folder, so a LudensScope key can never unlock LudensMatch.
    inline static juce::String productName { "LudensScope" };

    static void setProduct (const juce::String& product)
    {
        productName = product;
        resetCache();
    }

    // Keys issued on or before this date predate activation and stay unlimited.
    static constexpr const char* kActivationCutoffDate = "2026-11-19";
    static constexpr int kMaxMachines = 2;

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
    static const juce::String& activePublicKey()
    {
       #ifdef LUDENSCOPE_TEST_HOOKS
        if (publicKeyOverride.isNotEmpty())
            return publicKeyOverride;
       #endif

        static const juce::String key (kPublicKey);
        return key;
    }

    static Info parse (const juce::String& key,
                       const juce::String& publicKeyString = activePublicKey())
    {
        Info info;

        const auto parts = juce::StringArray::fromTokens (key.trim(), "|", "");
        if (parts.size() != 3 || parts[0] != "LS1")
        {
            info.error = "Not a " + productName + " licence key.";
            return info;
        }

        const auto payloadBytes = decode (parts[1]);
        const auto payload = juce::String::fromUTF8 (
            static_cast<const char*> (payloadBytes.getData()),
            (int) payloadBytes.getSize());

        const auto fields = juce::StringArray::fromTokens (payload, "|", "");
        if (fields.size() != 4 || fields[0] != productName || fields[1] != "FULL")
        {
            info.error = "Licence payload is not a " + productName + " PRO key.";
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
                           const juce::String& publicKeyString = activePublicKey())
    {
        return parse (key, publicKeyString).valid;
    }

    //==============================================================================
    // Products are independent: each one may move to the macOS convention on
    // its own schedule. LudensScope has shipped with ~/Library/<product> since
    // 1.0, so it keeps that path; LudensMatch starts in Application Support.
    inline static bool useApplicationSupportFolder = false;

    static juce::File folder()
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

       #if JUCE_MAC
        if (useApplicationSupportFolder)
            base = base.getChildFile ("Application Support");
       #endif

        return base.getChildFile (productName);
    }

    static juce::File file()
    {
       #ifdef LUDENSCOPE_TEST_HOOKS
        if (licenceDirectoryOverride.isDirectory())
            return licenceDirectoryOverride.getChildFile ("licence.txt");
       #endif

        return folder().getChildFile ("licence.txt");
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
        const auto trimmed = key.trim();
        const auto info = parse (trimmed);
        if (! info.valid)
        {
            error = info.error;
            return false;
        }

        error = machineLimitError (trimmed);
        if (error.isNotEmpty())
            return false;

        save (trimmed);
        error.clear();
        return true;
    }

    //==============================================================================
    static bool isUnlocked()
    {
       #ifdef LUDENSCOPE_TEST_HOOKS
        if (testingUnlock)
            return true;
       #endif

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
            return "PRO";
        if (cachedState == 2)
            return "LEGACY PRO";
        if (cachedState == 3)
            return "MACHINE LIMIT";

        return "LITE";
    }

    static bool isMachineLimitReached()
    {
        if (cachedState < 0)
            refresh();

        return cachedState == 3;
    }

    static juce::String activationMessage()
    {
        if (cachedState < 0)
            refresh();

        return cachedMessage;
    }

    static juce::String licensedEmail()
    {
        if (cachedState < 0)
            refresh();

        return cachedEmail;
    }

    //==============================================================================
   #ifdef LUDENSCOPE_TEST_HOOKS
    // Compiled only into the test binary; absent from every shipping plugin.
    static void setTestingUnlock (bool shouldBeUnlocked)
    {
        testingUnlock = shouldBeUnlocked;
        cachedState = shouldBeUnlocked ? 1 : -1;
    }

    static void setLicenceDirectoryForTesting (const juce::File& dir)
    {
        licenceDirectoryOverride = dir;
        resetCache();
    }

    static void setPublicKeyForTesting (const juce::String& key)
    {
        publicKeyOverride = key;
        resetCache();
    }

    static void setMachineHashesForTesting (const juce::StringArray& hashes)
    {
        machineHashesOverride = hashes;
    }
   #endif

    static void resetCache()
    {
        cachedState = -1;
        cachedEmail.clear();
        cachedMessage.clear();
    }

private:
    static juce::String shortHash (const juce::String& text)
    {
        return juce::SHA256 (text.toRawUTF8(), text.getNumBytesAsUTF8())
                   .toHexString().substring (0, 16);
    }

    static juce::File activationFile()
    {
        return file().getParentDirectory().getChildFile ("licence-machines.txt");
    }

    // Several identifiers are stored per machine: a MAC can change when a
    // network card is replaced, so any one of them identifying the machine
    // counts as the same activation.
    static juce::StringArray machineHashes()
    {
       #ifdef LUDENSCOPE_TEST_HOOKS
        if (machineHashesOverride.size() > 0)
            return machineHashesOverride;
       #endif

        juce::StringArray hashes;

        const auto ids = juce::SystemStats::getMachineIdentifiers (
            juce::SystemStats::MachineIdFlags::uniqueId
          | juce::SystemStats::MachineIdFlags::fileSystemId
          | juce::SystemStats::MachineIdFlags::macAddresses);

        for (const auto& id : ids)
            if (id.isNotEmpty())
                hashes.add (shortHash (id));

        hashes.removeDuplicates (false);
        return hashes;
    }

    // Empty result means the key may be used here. Otherwise it explains that
    // both activations are already taken.
    static juce::String machineLimitError (const juce::String& key)
    {
        const auto info = parse (key);
        if (! info.valid)
            return {};

        if (info.issued.compare (kActivationCutoffDate) <= 0)
            return {};

        const auto mine = machineHashes();
        if (mine.isEmpty())
            return {};

        const auto keyId = shortHash (key);
        auto lines = juce::StringArray::fromLines (activationFile().loadFileAsString());
        juce::StringArray kept;
        int used = 0;
        bool activatedHere = false;

        for (const auto& line : lines)
        {
            const auto cols = juce::StringArray::fromTokens (line, "|", "");

            if (cols.size() < 2 || cols[0] != keyId)
            {
                kept.add (line);
                continue;
            }

            const auto recorded = juce::StringArray::fromTokens (cols[1], ",", "");
            bool matches = false;

            for (const auto& hash : mine)
                if (recorded.contains (hash))
                {
                    matches = true;
                    break;
                }

            if (matches)
                activatedHere = true;

            kept.add (line);
            ++used;
        }

        if (! activatedHere && used >= kMaxMachines)
            return "This key is already active on " + juce::String (kMaxMachines)
                   + " machines. Email alek.ludens@gmail.com to move it to another machine.";

        if (! activatedHere)
        {
            kept.add (keyId + "|" + mine.joinIntoString (","));

            auto f = activationFile();
            f.getParentDirectory().createDirectory();
            f.replaceWithText (kept.joinIntoString ("\n") + "\n");
        }

        return {};
    }

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
               .replaceWithText ("Grandfathered PRO licence (0.4.x install)\n");
            return true;
        }

        return false;
    }

    static void refresh()
    {
       #ifdef LUDENSCOPE_TEST_HOOKS
        if (testingUnlock)
        {
            cachedState = 1;
            return;
        }
       #endif

        cachedState = 0;
        cachedEmail.clear();
        cachedMessage.clear();

        const auto key = load();
        if (key.isNotEmpty())
        {
            const auto info = parse (key);
            if (info.valid)
            {
                cachedEmail = info.email;
                cachedMessage = machineLimitError (key);
                cachedState = cachedMessage.isEmpty() ? 1 : 3;
                return;
            }
        }

        if (legacyInstalled())
            cachedState = 2;
    }

   #ifdef LUDENSCOPE_TEST_HOOKS
    static inline bool testingUnlock = false;
    static inline juce::File licenceDirectoryOverride;
    static inline juce::String publicKeyOverride;
    static inline juce::StringArray machineHashesOverride;
   #endif
    static inline int cachedState = -1;
    static inline juce::String cachedEmail;
    static inline juce::String cachedMessage;
};
