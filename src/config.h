#pragma once

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/value_codecs.h"

#include <string>
#include <string_view>

namespace metroex {

namespace legacy {
struct Config;
enum class ReadStatus;
}  // namespace legacy

// The game's name as cameraunlock-core's data/games.json spells it. The settings file's first
// line names it.
constexpr char kGameDisplayName[] = "Metro Exodus Enhanced Edition";

// Both beside the exe, like the log, and NOT beside the .asi.
//
// Ultimate ASI Loader scans `scripts\` and `plugins\` as well as the exe
// directory, so the two can be different folders. When they are, an .asi-relative
// config lands somewhere the player was never told about, the mod writes a fresh
// default file there, and the one at the game root that the README, the Nexus
// page and the launcher manifest all name is read by nobody. Resolving beside the
// exe makes the config, the log and every document agree on one directory.
constexpr wchar_t kConfigFileName[] = L"CameraUnlock.ini";

// The file every build before the canonical format read. The owner imports it while
// CameraUnlock.ini is absent and never writes, renames or deletes it, so an older build
// still reads it after a rollback.
constexpr wchar_t kLegacyConfigFileName[] = L"MetroExodusHeadTracking.ini";

struct Config : cameraunlock::HeadTrackingConfig {
    // Field of view in degrees, or 0 to leave the game's own setting alone.
    //
    // The game's Field of View slider stops at 75 because the console variable
    // behind it carries that bound and its setter rejects anything past it. A
    // non-zero value here widens the bound and writes the setting, so it is the
    // same number the slider sets and it reaches the whole engine rather than
    // only the picture. See fov.h.
    float fov_override = 0.0f;

    // Per-frame camera logging. It exists for re-deriving the camera layout if a
    // game patch ever changes it, and costs megabytes of log an hour, so it is
    // opt-in.
    bool discovery = false;
};

// [Camera] FieldOfView: 0, the game's own setting, or 60 to 120 degrees. The floor is the
// game's own, because the slider has never gone below it and the engine scales the HUD by 60
// divided by the base field of view, so a narrower one draws a larger HUD. The ceiling is well
// short of the 179 degrees the engine hard-clamps the camera to, which leaves room for the
// wide-angle cameras the game uses in vehicles and cutscenes to stay under that clamp.
class FovCodec {
public:
    using Value = float;

    static constexpr float kMin = 60.0f;
    static constexpr float kMax = 120.0f;

    cameraunlock::config::CodecParseResult<float> Parse(std::string_view text) const;
    // Throws std::invalid_argument for a value Parse would not read back.
    std::string Render(float value) const;
    bool Equal(float a, float b) const { return angle_.Equal(a, b); }

private:
    cameraunlock::config::FloatCodec angle_{0.0f, kMax};
};

// Every row of the settings file. WorldSpaceYaw and the tracking-mode pair are the rows the
// hotkeys save; End changes the session only.
cameraunlock::config::ConfigTable<Config> ConfigTable();

// Reads a pre-canonical file through the frozen reader in legacy_config/, then maps it.
cameraunlock::config::LegacyImport<Config> LegacyConfigImport();

// The map from what the frozen reader read into the settings the mod runs on.
cameraunlock::config::ImportResult MapLegacyConfig(legacy::ReadStatus status, const legacy::Config& read,
                                                   Config& out);

// `path` is CameraUnlock.ini and `legacyPath` the MetroExodusHeadTracking.ini beside it, both
// fully qualified. The mod passes DefaultsFile::PerUser() and a test a scratch file.
cameraunlock::config::ConfigOwnerOptions<Config> ConfigOwnerOptionsFor(const std::wstring& path,
                                                                       const std::wstring& legacyPath,
                                                                       cameraunlock::config::DefaultsFile defaults);

}  // namespace metroex
