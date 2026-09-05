// The INI in the repo root against the one the mod writes for itself.
//
// MetroExodusHeadTracking.ini ships at the root of the release ZIP, is copied
// into the Nexus layout, and is carried base64-encoded inside
// launcher-manifest.json so the launcher can seed it. All of that is a copy of
// what WriteDefaultIni() in src/config.cpp produces, kept in step by hand, and
// nothing at build or package time compared the two.
//
// So they drifted: the shipped [Camera] comment went out without the sentence
// saying that a non-zero FieldOfView writes six bytes into the running game's
// code. A player who installed from the ZIP was
// told less about what the mod does to their game than one whose file was
// written on first launch. That is the class of bug this suite exists to catch,
// and it is invisible to the value round-trip in config_tests.cpp, because every
// value was right - only the prose a player reads was wrong.
//
// The manifest's own copy drifted the same way and for longer, because the only
// thing that ever compared it to anything was package-release.ps1 restamping it
// on the way into the ZIP - and a stamp that overwrites cannot report what it
// overwrote. The repo copy is what a reader, a reviewer and anything consuming
// the checked-in manifest sees, so it is compared here rather than trusted to
// the packager.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

#include <windows.h>

#include "config.h"
#include "test_harness.h"

namespace {

using metroex_test::Check;

// Enough base64 to read one JSON string value back. Not a general decoder: it
// refuses anything outside the alphabet rather than skipping it, so a blob the
// launcher would choke on fails here instead of decoding to something plausible.
bool DecodeBase64(const std::string& encoded, std::string& out) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    out.clear();
    uint32_t accumulator = 0;
    int bits = 0;
    size_t padding = 0;
    for (const char c : encoded) {
        if (c == '=') {
            ++padding;
            continue;
        }
        if (padding != 0) return false;
        const char* found = std::strchr(kAlphabet, c);
        if (found == nullptr || c == '\0') return false;
        accumulator = (accumulator << 6) | static_cast<uint32_t>(found - kAlphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((accumulator >> bits) & 0xFF));
        }
    }
    return padding <= 2 && (encoded.size() % 4) == 0;
}

// The one `content_b64` value in the manifest, by hand rather than through a
// JSON parser: the test binaries link against nothing but the core, and the
// value is a flat string with no escapes in it.
bool ReadManifestSeed(const std::string& manifest, std::string& encoded) {
    const std::string key = "\"content_b64\"";
    const size_t keyAt = manifest.find(key);
    if (keyAt == std::string::npos) return false;
    if (manifest.find(key, keyAt + key.size()) != std::string::npos) return false;

    const size_t openQuote = manifest.find('"', manifest.find(':', keyAt) + 1);
    if (openQuote == std::string::npos) return false;
    const size_t closeQuote = manifest.find('"', openQuote + 1);
    if (closeQuote == std::string::npos) return false;

    encoded = manifest.substr(openQuote + 1, closeQuote - openQuote - 1);
    return !encoded.empty();
}

// Carriage returns are dropped before comparing. The repo's copy is a checked-in
// text file whose line endings are whatever git's end-of-line handling left in
// the working tree, and the mod's copy is written by IniWriter at runtime, so
// the two are subject to different conventions on the same machine. What this
// suite can answer is whether they say the same thing.
std::string ReadTextBytes(const char* path) {
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (f == nullptr) return {};

    std::string out;
    char chunk[4096];
    for (size_t n = std::fread(chunk, 1, sizeof(chunk), f); n != 0;
         n = std::fread(chunk, 1, sizeof(chunk), f)) {
        out.append(chunk, n);
    }
    std::fclose(f);
    return out;
}

std::string ReadTextWithoutCarriageReturns(const char* path) {
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (f == nullptr) return {};

    std::string out;
    char chunk[4096];
    for (size_t n = std::fread(chunk, 1, sizeof(chunk), f); n != 0;
         n = std::fread(chunk, 1, sizeof(chunk), f)) {
        for (size_t i = 0; i < n; ++i) {
            if (chunk[i] != '\r') out.push_back(chunk[i]);
        }
    }
    std::fclose(f);
    return out;
}

void TheShippedIniSaysWhatTheModWouldWrite() {
    char temp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp);
    const std::string path = std::string(temp) + "metroexodus_ht_shipped_ini.ini";
    DeleteFileA(path.c_str());

    metroex::Config written;
    Check(written.LoadOrCreate(path.c_str()),
          "the mod writes its default INI when there is none");

    const std::string generated = ReadTextWithoutCarriageReturns(path.c_str());
    const std::string shipped = ReadTextWithoutCarriageReturns(METROEX_SHIPPED_INI);
    Check(!generated.empty(), "the written default file has content");
    Check(!shipped.empty(), "the shipped MetroExodusHeadTracking.ini was found and read");
    Check(generated == shipped,
          "the shipped MetroExodusHeadTracking.ini is what the mod writes for itself - "
          "regenerate it from WriteDefaultIni in src/config.cpp");
}

// The launcher never opens plugins/MetroExodusHeadTracking.ini. It seeds the
// player's config from the base64 blob inside launcher-manifest.json, so that
// blob is a third copy of the same file and drifts like the second one did -
// silently, and into the config of every player who installs through Lopari
// rather than by hand.
void TheManifestSeedsTheSameIni() {
    const std::string manifest = ReadTextWithoutCarriageReturns(METROEX_LAUNCHER_MANIFEST);
    Check(!manifest.empty(), "launcher-manifest.json was found and read");

    std::string encoded;
    Check(ReadManifestSeed(manifest, encoded),
          "launcher-manifest.json carries exactly one content_b64 seed value");

    std::string decoded;
    Check(DecodeBase64(encoded, decoded), "the manifest's content_b64 is valid base64");

    std::string withoutCarriageReturns;
    for (const char c : decoded) {
        if (c != '\r') withoutCarriageReturns.push_back(c);
    }

    const std::string shipped = ReadTextWithoutCarriageReturns(METROEX_SHIPPED_INI);
    Check(withoutCarriageReturns == shipped,
          "launcher-manifest.json's seed is the shipped MetroExodusHeadTracking.ini - "
          "re-encode it from that file");

    // And byte for byte, carriage returns included. The comparison above cannot
    // see a line-ending difference, which is exactly what `*.ini text eol=crlf`
    // in .gitattributes exists to prevent: without it the INI checks out LF on
    // CI and stays CRLF on a maintainer's machine, and the two would base64 into
    // two different seeds with nothing reporting it.
    Check(decoded.size() == ReadTextBytes(METROEX_SHIPPED_INI).size(),
          "the manifest's seed has the same line endings as the shipped INI - "
          ".gitattributes must keep *.ini at CRLF");
}

// Every key the reader looks for has to be in the file the writer produces.
//
// The value round-trip in config_tests.cpp cannot see a key going missing: the
// reader's fallback for an absent key IS the member initialiser it is compared
// against, so a file with no `LimitZ=` line parses back to exactly the same
// struct. The comparison above cannot see it either, because regenerating the
// root INI from a writer that has stopped emitting a key keeps the two copies
// identical. What a player loses is a documented knob they can no longer find in
// their own config, with nothing saying where it went.
void TheGeneratedFileCarriesEveryKeyTheReaderLooksFor() {
    // Section-qualified, because AdsMode is read from two of them: [Hotkeys]
    // carries the key code and [View] carries the mode. An unqualified search
    // would let either stand in for the other, and the one that matters is the
    // one Insert writes back to.
    struct Key {
        const char* section;
        const char* key;
    };
    static const Key kKeys[] = {
        {"General", "EnableOnStartup"},   {"General", "Port"},
        {"General", "WorldSpaceYaw"},
        {"Sensitivity", "Yaw"},           {"Sensitivity", "Pitch"},
        {"Sensitivity", "Roll"},          {"Sensitivity", "InvertYaw"},
        {"Sensitivity", "InvertPitch"},   {"Sensitivity", "InvertRoll"},
        {"Smoothing", "LocalSmoothing"},  {"Smoothing", "RemoteSmoothing"},
        {"Position", "Enabled"},          {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},     {"Position", "SensitivityZ"},
        {"Position", "LimitX"},           {"Position", "LimitY"},
        {"Position", "LimitYDown"},       {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Hotkeys", "Toggle"},            {"Hotkeys", "CycleMode"},
        {"Hotkeys", "YawMode"},           {"Hotkeys", "AdsMode"},
        {"Hotkeys", "ChordToggle"},       {"Hotkeys", "ChordCycleMode"},
        {"Hotkeys", "ChordYawMode"},      {"Hotkeys", "ChordAdsMode"},
        {"View", "AdsMode"},
        {"Camera", "FieldOfView"},        {"Camera", "Discovery"},
    };

    const std::string shipped = ReadTextWithoutCarriageReturns(METROEX_SHIPPED_INI);
    Check(!shipped.empty(), "the shipped MetroExodusHeadTracking.ini was found and read");

    for (const Key& entry : kKeys) {
        const std::string header = std::string("[") + entry.section + "]\n";
        const size_t start = shipped.find(header);
        Check(start != std::string::npos,
              (std::string("the shipped INI carries a [") + entry.section + "] section").c_str());
        if (start == std::string::npos) continue;
        // Bounded by the next section header, so a key in a later section cannot
        // answer for this one.
        const size_t end = shipped.find("\n[", start + header.size());
        const std::string section = shipped.substr(start, end - start);
        // Anchored on a line start and followed by `=`, so a key named inside a
        // comment does not stand in for the key itself.
        const std::string needle = std::string("\n") + entry.key + "=";
        Check(section.find(needle) != std::string::npos,
              (std::string("the shipped INI carries [") + entry.section + "] " + entry.key)
                  .c_str());
    }
}

}  // namespace

int main() {
    TheShippedIniSaysWhatTheModWouldWrite();
    TheManifestSeedsTheSameIni();
    TheGeneratedFileCarriesEveryKeyTheReaderLooksFor();

    return metroex_test::Report();
}
