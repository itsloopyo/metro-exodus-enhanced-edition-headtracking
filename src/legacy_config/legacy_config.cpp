#include "legacy_config.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"
#include "cameraunlock/logging/file_log.h"

#include <cctype>
#include <string>

#include <windows.h>

namespace metroex::legacy {

namespace {

namespace Log = ::cameraunlock::logging;

bool FileExists(const char* path) {
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Every float in this file is read through the shared guards rather than
// IniReader's own readers, because those parse a PREFIX: "nan" and a literal
// that overflows to infinity are accepted whole, and "0,15" - a European
// decimal comma, which is the expected user typo - reads back as 0.0 and passes
// every range check silently. The first two reach the camera transform and
// write a NaN view matrix every frame with nothing in the log. See
// cameraunlock/config/value_guards.h for the full list of hazards.
constexpr cameraunlock::config::LogSink kLogSink = &Log::Line;

// Sign and magnitude are both legitimate tuning choices, so a sensitivity is
// only refused where it would reach the camera matrix as garbage.
float ReadSensitivity(const cameraunlock::IniReader& ini, const char* section, const char* key,
                      float fallback) {
    using cameraunlock::config::kMaxSensitivity;
    return cameraunlock::config::ReadFloatChecked(ini, section, key, fallback, -kMaxSensitivity,
                                                  kMaxSensitivity, kLogSink);
}

float ReadSmoothing(const cameraunlock::IniReader& ini, const char* key, float fallback) {
    return cameraunlock::config::ReadFloatChecked(ini, "Smoothing", key, fallback, 0.0f, 1.0f,
                                                  kLogSink);
}

// A negative limit inverts the clamp in PositionProcessor, which pins the lean
// at a fixed offset instead of freeing it, so zero is the floor.
float ReadPositionLimit(const cameraunlock::IniReader& ini, const char* key, float fallback) {
    return cameraunlock::config::ReadFloatChecked(ini, "Position", key, fallback, 0.0f,
                                                  cameraunlock::config::kMaxPositionLimit,
                                                  kLogSink);
}

// Every bool in this file is read from the RAW value rather than through
// IniReader::ReadBool, for the same reason every float goes through the shared
// guards: the core reader compares the WHOLE string against a fixed set, and
// GetPrivateProfileStringA does not strip an inline comment. So `Enabled=0 ; no
// 6dof` matches nothing and falls back to the default - positional tracking
// stays on, and the log says nothing about the line the player just edited.
//
// ReadRawValue truncates at `;` or `#` and trims, so what is compared here is
// what the player meant. An absent or empty key is the ordinary case and says
// nothing; a value that is present and is not a word this reader knows is
// reported and falls back.
//
// The accepted words mirror IniReader::ReadBool's, folded to lower case, so a
// file written for either reader loads the same way under both.
bool ReadBoolChecked(const cameraunlock::IniReader& ini, const char* section, const char* key,
                     bool fallback) {
    std::string raw = cameraunlock::config::ReadRawValue(ini, section, key);
    if (raw.empty()) return fallback;
    for (char& c : raw) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (raw == "1" || raw == "true" || raw == "yes" || raw == "on") return true;
    if (raw == "0" || raw == "false" || raw == "no" || raw == "off") return false;
    Log::Line("config: [%s] %s=%s is not true or false, so the default %s is used instead",
              section, key, raw.c_str(), fallback ? "true" : "false");
    return fallback;
}

// GetAsyncKeyState only defines 0x01..0xFE and the poller's chord guard owns the
// modifiers, so a binding outside that registers a hotkey that can never fire
// and the key silently does nothing.
int ReadHotkey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const int vk = ini.ReadHex("Hotkeys", key, fallback);
    if (cameraunlock::config::IsBindableVirtualKey(vk)) return vk;
    Log::Line("config: [Hotkeys] %s=0x%02X is not a key that can be polled, so the default 0x%02X "
              "is used instead",
              key, vk, fallback);
    return fallback;
}

// False when the port is outside the unprivileged range, which is fatal: a mod
// that quietly listened on a different port than the file names would look like
// a tracker that never connected.
bool ReadGeneralSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.enabled_on_startup =
        ReadBoolChecked(ini, "General", "EnableOnStartup", kDefaultEnableOnStartup);
    cfg.world_space_yaw =
        ReadBoolChecked(ini, "General", "WorldSpaceYaw", kDefaultWorldSpaceYaw);

    const int port = ini.ReadInt("General", "Port", kDefaultUdpPort);
    if (port < kMinUdpPort || port > kMaxUdpPort) {
        Log::Line("ERROR: INI port %d out of range %d-%d", port, kMinUdpPort, kMaxUdpPort);
        return false;
    }
    cfg.udp_port = static_cast<uint16_t>(port);
    return true;
}

void ReadSensitivitySection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.sens_yaw = ReadSensitivity(ini, "Sensitivity", "Yaw", kDefaultSensitivity);
    cfg.sens_pitch = ReadSensitivity(ini, "Sensitivity", "Pitch", kDefaultSensitivity);
    cfg.sens_roll = ReadSensitivity(ini, "Sensitivity", "Roll", kDefaultSensitivity);
    cfg.invert_yaw = ReadBoolChecked(ini, "Sensitivity", "InvertYaw", kDefaultInvert);
    cfg.invert_pitch = ReadBoolChecked(ini, "Sensitivity", "InvertPitch", kDefaultInvert);
    cfg.invert_roll = ReadBoolChecked(ini, "Sensitivity", "InvertRoll", kDefaultInvert);
}

void ReadSmoothingSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.local_smoothing = ReadSmoothing(ini, "LocalSmoothing", kDefaultLocalSmoothing);
    cfg.remote_smoothing = ReadSmoothing(ini, "RemoteSmoothing", kDefaultRemoteSmoothing);
}

void ReadPositionSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.position_enabled =
        ReadBoolChecked(ini, "Position", "Enabled", kDefaultPositionEnabled);
    cfg.pos_sens_x =
        ReadSensitivity(ini, "Position", "SensitivityX", kDefaultPositionSensitivity);
    cfg.pos_sens_y =
        ReadSensitivity(ini, "Position", "SensitivityY", kDefaultPositionSensitivity);
    cfg.pos_sens_z =
        ReadSensitivity(ini, "Position", "SensitivityZ", kDefaultPositionSensitivity);
    cfg.pos_limit_x = ReadPositionLimit(ini, "LimitX", kDefaultPosLimitX);
    cfg.pos_limit_y = ReadPositionLimit(ini, "LimitY", kDefaultPosLimitY);
    // Falls back to whatever LimitY resolved to, not to the 0.20 default: a config
    // that sets only LimitY would otherwise keep 0.20 m of downward travel while the
    // upward budget moved, with nothing saying the key was half-effective.
    cfg.pos_limit_y_down = ReadPositionLimit(ini, "LimitYDown", cfg.pos_limit_y);
    cfg.pos_limit_z = ReadPositionLimit(ini, "LimitZ", kDefaultPosLimitZ);
    cfg.pos_limit_z_back = ReadPositionLimit(ini, "LimitZBack", kDefaultPosLimitZBack);
}

void ReadHotkeysSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.vk_toggle = ReadHotkey(ini, "Toggle", kDefaultVkToggle);
    cfg.vk_cycle_mode = ReadHotkey(ini, "CycleMode", kDefaultVkCycleMode);
    cfg.vk_yaw_mode = ReadHotkey(ini, "YawMode", kDefaultVkYawMode);
    cfg.chord_toggle = ReadBoolChecked(ini, "Hotkeys", "ChordToggle", kDefaultChordEnabled);
    cfg.chord_cycle_mode =
        ReadBoolChecked(ini, "Hotkeys", "ChordCycleMode", kDefaultChordEnabled);
    cfg.chord_yaw_mode =
        ReadBoolChecked(ini, "Hotkeys", "ChordYawMode", kDefaultChordEnabled);
}

// False when FieldOfView is neither the off switch nor inside the accepted
// range, which is fatal rather than clamped: the value is written into the
// engine's own setting every frame, and a clamp would leave the player looking
// at a field of view they did not ask for with nothing but a log line saying so.
bool ReadCameraSection(const cameraunlock::IniReader& ini, Config& cfg) {
    // 0 is the off switch, not a field of view, so it is the one value outside
    // the range that is not an error. Written as a positive range test rather
    // than a pair of `<` / `>` rejections so a NaN - which strtod accepts from
    // "nan" and produces from an overflowing literal - fails it instead of
    // failing both halves and being written into the engine's own setting every
    // frame.
    cfg.fov_override = ini.ReadFloat("Camera", "FieldOfView", kDefaultFovOverride);
    const bool fovOff = cfg.fov_override == kDefaultFovOverride;
    const bool fovInRange =
        cfg.fov_override >= kMinFovOverride && cfg.fov_override <= kMaxFovOverride;
    if (!fovOff && !fovInRange) {
        Log::Line("ERROR: INI FieldOfView %.1f out of range %.0f-%.0f (0 leaves the game's own "
                  "setting alone)",
                  static_cast<double>(cfg.fov_override), static_cast<double>(kMinFovOverride),
                  static_cast<double>(kMaxFovOverride));
        return false;
    }

    cfg.discovery = ReadBoolChecked(ini, "Camera", "Discovery", kDefaultDiscovery);
    cfg.light_follows_head =
        ReadBoolChecked(ini, "Light", "LightFollowsHead", kDefaultLightFollowsHead);
    cfg.light_multiplier = cameraunlock::config::ReadFloatChecked(
        ini, "Light", "LightMultiplier", kDefaultLightMultiplier, 0.0f, kMaxLightMultiplier,
        kLogSink);
    return true;
}

}  // namespace

ReadStatus Read(const char* iniPath, Config& cfg) {
    if (!FileExists(iniPath)) return ReadStatus::Absent;

    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return ReadStatus::OpenFailed;
    }

    if (!ReadGeneralSection(ini, cfg)) return ReadStatus::PortRefused;
    ReadSensitivitySection(ini, cfg);
    ReadSmoothingSection(ini, cfg);
    ReadPositionSection(ini, cfg);
    ReadHotkeysSection(ini, cfg);
    if (!ReadCameraSection(ini, cfg)) return ReadStatus::FovRefused;
    return ReadStatus::Read;
}

}  // namespace metroex::legacy
