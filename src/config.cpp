#include "config.h"

#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"

#include <cctype>

#include <windows.h>

namespace metroex {

namespace {

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

// One reader per section of the file, mirroring the writers below so a key
// added to one is missing from the other in the same place. The two that can
// refuse the file say so with a bool; the rest cannot fail, because every value
// they read carries a fallback.

// False when the port is outside the unprivileged range, which is fatal: a mod
// that quietly listened on a different port than the file names would look like
// a tracker that never connected.
bool ReadGeneralSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.enabled_on_startup =
        ReadBoolChecked(ini, "General", "EnableOnStartup", defaults::kEnableOnStartup);
    cfg.world_space_yaw =
        ReadBoolChecked(ini, "General", "WorldSpaceYaw", defaults::kWorldSpaceYaw);

    const int port = ini.ReadInt("General", "Port", defaults::kUdpPort);
    if (port < kMinUdpPort || port > kMaxUdpPort) {
        Log::Line("ERROR: INI port %d out of range %d-%d", port, kMinUdpPort, kMaxUdpPort);
        return false;
    }
    cfg.udp_port = static_cast<uint16_t>(port);
    return true;
}

void ReadSensitivitySection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.sens_yaw = ReadSensitivity(ini, "Sensitivity", "Yaw", defaults::kSensitivity);
    cfg.sens_pitch = ReadSensitivity(ini, "Sensitivity", "Pitch", defaults::kSensitivity);
    cfg.sens_roll = ReadSensitivity(ini, "Sensitivity", "Roll", defaults::kSensitivity);
    cfg.invert_yaw = ReadBoolChecked(ini, "Sensitivity", "InvertYaw", defaults::kInvert);
    cfg.invert_pitch = ReadBoolChecked(ini, "Sensitivity", "InvertPitch", defaults::kInvert);
    cfg.invert_roll = ReadBoolChecked(ini, "Sensitivity", "InvertRoll", defaults::kInvert);
}

void ReadSmoothingSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.local_smoothing =
        ReadSmoothing(ini, "LocalSmoothing",
                      static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing));
    cfg.remote_smoothing =
        ReadSmoothing(ini, "RemoteSmoothing",
                      static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing));
}

void ReadPositionSection(const cameraunlock::IniReader& ini, Config& cfg) {
    const cameraunlock::PositionSettings posDefaults;
    cfg.position_enabled =
        ReadBoolChecked(ini, "Position", "Enabled", defaults::kPositionEnabled);
    cfg.pos_sens_x =
        ReadSensitivity(ini, "Position", "SensitivityX", defaults::kPositionSensitivity);
    cfg.pos_sens_y =
        ReadSensitivity(ini, "Position", "SensitivityY", defaults::kPositionSensitivity);
    cfg.pos_sens_z =
        ReadSensitivity(ini, "Position", "SensitivityZ", defaults::kPositionSensitivity);
    cfg.pos_limit_x = ReadPositionLimit(ini, "LimitX", posDefaults.limit_x);
    cfg.pos_limit_y = ReadPositionLimit(ini, "LimitY", posDefaults.limit_y);
    // Falls back to whatever LimitY resolved to, not to the 0.20 default: a config
    // that sets only LimitY would otherwise keep 0.20 m of downward travel while the
    // upward budget moved, with nothing saying the key was half-effective.
    cfg.pos_limit_y_down = ReadPositionLimit(ini, "LimitYDown", cfg.pos_limit_y);
    cfg.pos_limit_z = ReadPositionLimit(ini, "LimitZ", posDefaults.limit_z);
    cfg.pos_limit_z_back = ReadPositionLimit(ini, "LimitZBack", posDefaults.limit_z_back);
}

void ReadHotkeysSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.vk_toggle = ReadHotkey(ini, "Toggle", defaults::kVkToggle);
    cfg.vk_cycle_mode = ReadHotkey(ini, "CycleMode", defaults::kVkCycleMode);
    cfg.vk_yaw_mode = ReadHotkey(ini, "YawMode", defaults::kVkYawMode);
    cfg.vk_ads_mode = ReadHotkey(ini, "AdsMode", defaults::kVkAdsMode);
    cfg.chord_toggle = ReadBoolChecked(ini, "Hotkeys", "ChordToggle", defaults::kChordEnabled);
    cfg.chord_cycle_mode =
        ReadBoolChecked(ini, "Hotkeys", "ChordCycleMode", defaults::kChordEnabled);
    cfg.chord_yaw_mode =
        ReadBoolChecked(ini, "Hotkeys", "ChordYawMode", defaults::kChordEnabled);
    cfg.chord_ads_mode =
        ReadBoolChecked(ini, "Hotkeys", "ChordAdsMode", defaults::kChordEnabled);
}

void ReadViewSection(const cameraunlock::IniReader& ini, Config& cfg) {
    // ParseAdsMode answers with the default for anything that is not one of the
    // three values, rather than falling through to whichever branch is last.
    // That covers a typo in a hand-edited file, a key this release has not
    // written yet, and a mode renamed since an older release wrote the file: all
    // three land the player on stock ADS rather than on head tracking through
    // their sights that they never asked for.
    std::string raw = cameraunlock::config::ReadRawValue(ini, "View", "AdsMode");
    cfg.ads_mode = ParseAdsMode(raw.empty() ? AdsModeValue(kDefaultAdsMode) : raw.c_str());
    if (raw.empty()) return;
    // Folded before comparing, because ParseAdsMode folds too: `AdsMode=Tracked`
    // is accepted and honoured, and reporting it as refused would name the mode
    // the player asked for as the one it fell back to.
    for (char& c : raw) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (raw != AdsModeValue(cfg.ads_mode)) {
        Log::Line("config: [View] AdsMode=%s is not one of paused, marker or tracked, so %s is "
                  "used instead",
                  raw.c_str(), AdsModeValue(cfg.ads_mode));
    }
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
    cfg.fov_override = ini.ReadFloat("Camera", "FieldOfView", defaults::kFovOverride);
    const bool fovOff = cfg.fov_override == defaults::kFovOverride;
    const bool fovInRange =
        cfg.fov_override >= kMinFovOverride && cfg.fov_override <= kMaxFovOverride;
    if (!fovOff && !fovInRange) {
        Log::Line("ERROR: INI FieldOfView %.1f out of range %.0f-%.0f (0 leaves the game's own "
                  "setting alone)",
                  static_cast<double>(cfg.fov_override), static_cast<double>(kMinFovOverride),
                  static_cast<double>(kMaxFovOverride));
        return false;
    }

    cfg.discovery = ReadBoolChecked(ini, "Camera", "Discovery", defaults::kDiscovery);
    cfg.light_follows_head =
        ReadBoolChecked(ini, "Light", "LightFollowsHead", defaults::kLightFollowsHead);
    cfg.light_multiplier = cameraunlock::config::ReadFloatChecked(
        ini, "Light", "LightMultiplier", defaults::kLightMultiplier, 0.0f,
        cameraunlock::effects::kMaxLightMultiplier, kLogSink);
    return true;
}

// One writer per section of the file, in the order they appear in it. Each owns
// the blank line above its own header, so the seam between two sections sits in
// one place rather than at the end of whichever one happens to come first.
void WriteGeneralSection(cameraunlock::IniWriter& w) {
    w.WriteBlankLine();
    w.WriteSection("General");
    w.WriteBool("EnableOnStartup", defaults::kEnableOnStartup);
    w.WriteComment(" UDP port to listen on, 1024 to 65535. A value outside that range stops the");
    w.WriteComment(" mod loading rather than being ignored.");
    w.WriteInt("Port", defaults::kUdpPort);
    w.WriteComment(" Which up-axis head yaw turns about.");
    w.WriteComment("   true  - the world up-axis. Look at the floor and turn your head and");
    w.WriteComment("           you still pan across it, level with the horizon.");
    w.WriteComment("   false - the camera's own up-axis, which leans the view once the");
    w.WriteComment("           camera is pitched steeply.");
    w.WriteComment(" Page Down switches between the two while you play; this key is only");
    w.WriteComment(" what the mod starts on.");
    w.WriteBool("WorldSpaceYaw", defaults::kWorldSpaceYaw);
}

void WriteSensitivitySection(cameraunlock::IniWriter& w) {
    w.WriteBlankLine();
    w.WriteSection("Sensitivity");
    w.WriteDouble("Yaw", defaults::kSensitivity);
    w.WriteDouble("Pitch", defaults::kSensitivity);
    w.WriteDouble("Roll", defaults::kSensitivity);
    w.WriteBool("InvertYaw", defaults::kInvert);
    w.WriteBool("InvertPitch", defaults::kInvert);
    w.WriteBool("InvertRoll", defaults::kInvert);
}

void WriteSmoothingSection(cameraunlock::IniWriter& w) {
    w.WriteBlankLine();
    w.WriteSection("Smoothing");
    w.WriteComment(" Smoothing applied when the tracker runs on this machine (loopback).");
    w.WriteComment(" 0 = no smoothing, 1 = heavy.");
    w.WriteDouble("LocalSmoothing", cameraunlock::math::kDefaultLocalSmoothing);
    w.WriteComment(" Smoothing applied when the tracker is a remote device on the network.");
    w.WriteComment(" 0 = no smoothing, 1 = heavy.");
    w.WriteDouble("RemoteSmoothing", cameraunlock::math::kDefaultRemoteSmoothing);
}

void WritePositionSection(cameraunlock::IniWriter& w) {
    w.WriteBlankLine();
    w.WriteSection("Position");
    w.WriteComment(" Positional (6DOF) head tracking: leaning and moving your head.");
    w.WriteBool("Enabled", defaults::kPositionEnabled);
    w.WriteDouble("SensitivityX", defaults::kPositionSensitivity);
    w.WriteDouble("SensitivityY", defaults::kPositionSensitivity);
    w.WriteDouble("SensitivityZ", defaults::kPositionSensitivity);
    w.WriteComment(" Travel limits in metres.");
    w.WriteDouble("LimitX", cameraunlock::PositionSettings{}.limit_x);
    w.WriteDouble("LimitY", cameraunlock::PositionSettings{}.limit_y);
    w.WriteDouble("LimitYDown", cameraunlock::PositionSettings{}.limit_y_down);
    w.WriteDouble("LimitZ", cameraunlock::PositionSettings{}.limit_z);
    w.WriteDouble("LimitZBack", cameraunlock::PositionSettings{}.limit_z_back);
}

void WriteHotkeysSection(cameraunlock::IniWriter& w) {
    w.WriteBlankLine();
    w.WriteSection("Hotkeys");
    w.WriteComment(" Virtual-key codes. Defaults: End (toggle tracking),");
    w.WriteComment(" Page Up (cycle mode: rotation and position, rotation only,");
    w.WriteComment(" position only).");
    w.WriteHex("Toggle", defaults::kVkToggle);
    w.WriteHex("CycleMode", defaults::kVkCycleMode);
    w.WriteComment(" Page Down switches yaw between the world up-axis and the camera's own.");
    w.WriteHex("YawMode", defaults::kVkYawMode);
    w.WriteComment(" Insert cycles what head tracking does while the sights are up.");
    w.WriteHex("AdsMode", defaults::kVkAdsMode);
    w.WriteComment(" Chord alternatives: Ctrl+Shift+Y (toggle tracking),");
    w.WriteComment(" Ctrl+Shift+G (cycle mode), Ctrl+Shift+H (yaw mode),");
    w.WriteComment(" Ctrl+Shift+U (cycle ADS mode).");
    w.WriteBool("ChordToggle", defaults::kChordEnabled);
    w.WriteBool("ChordCycleMode", defaults::kChordEnabled);
    w.WriteBool("ChordYawMode", defaults::kChordEnabled);
    w.WriteBool("ChordAdsMode", defaults::kChordEnabled);
}

void WriteViewSection(cameraunlock::IniWriter& w) {
    w.WriteBlankLine();
    w.WriteSection("View");
    w.WriteComment(" What head tracking does while you are aiming down sights.");
    w.WriteComment("   paused  - the game keeps the camera until you lower the weapon.");
    w.WriteComment("   marker  - tracking carries on, and a marker is drawn where");
    w.WriteComment("             your rounds will land.");
    w.WriteComment("   tracked - tracking carries on, nothing drawn.");
    w.WriteComment(" Insert cycles the same three in that order and saves the choice");
    w.WriteComment(" back here. Anything else in this key reads as paused.");
    w.WriteString("AdsMode", AdsModeValue(kDefaultAdsMode));
}

void WriteCameraSection(cameraunlock::IniWriter& w) {
    w.WriteBlankLine();
    w.WriteSection("Camera");
    w.WriteComment(" Field of view in degrees, the same number the game's own Field of View");
    w.WriteComment(" slider sets. 0 leaves that slider alone. The game stops its slider at 75,");
    w.WriteComment(" and its engine holds the drawn picture at 60 in a level whatever the");
    w.WriteComment(" slider says; a value here goes past both, up to 120. The picture, the HUD");
    w.WriteComment(" scale and what the game bothers to draw all follow it, because it is the");
    w.WriteComment(" game's own setting rather than a change made behind the engine's back.");
    w.WriteComment(" Setting it also takes the engine's hold off, which is six bytes written");
    w.WriteComment(" into the running game's code, and it widens the bounds the game's own");
    w.WriteComment(" setter enforces. The setting and its bounds are put back when the game");
    w.WriteComment(" exits; the six bytes live only in memory and go with the process. Nothing");
    w.WriteComment(" is written when this is 0, and the main menu draws at 60 either way.");
    w.WriteComment(" Accepted: 0, or 60 to 120. Anything else stops the mod loading.");
    w.WriteDouble("FieldOfView", defaults::kFovOverride);
    w.WriteComment(" Per-frame camera logging: the pose the tracker sent, the camera the");
    w.WriteComment(" game published, and the camera the engine built the frame from. It is");
    w.WriteComment(" how a game patch that moved the camera gets re-derived, and how a");
    w.WriteComment(" report of the view going the wrong way gets answered. It writes");
    w.WriteComment(" megabytes an hour to the log; leave it off unless we ask you to turn");
    w.WriteComment(" it on.");
    w.WriteBool("Discovery", defaults::kDiscovery);
}

void WriteLightSection(cameraunlock::IniWriter& w) {
    w.WriteBlankLine();
    w.WriteSection("Light");
    w.WriteComment(" Whether your torch turns with your head instead of staying on your aim.");
    w.WriteBool("LightFollowsHead", defaults::kLightFollowsHead);
    w.WriteComment(" How far the beam leads your view. It is deliberately more than 1: when");
    w.WriteComment(" you turn your head you keep looking at what you turned towards, so your");
    w.WriteComment(" gaze sits past the middle of the screen, and a beam matched to the view");
    w.WriteComment(" alone lands short of it. Accepted: 0 to 5. 0 pins the beam to your aim,");
    w.WriteComment(" which is what the game does unmodded. Anything outside that range is");
    w.WriteComment(" refused and the beam stays on the aim.");
    w.WriteDouble("LightMultiplier", defaults::kLightMultiplier);
}

// The file a first launch leaves next to the exe: every key this release reads,
// each with the comment that says what it does, so a player never has to be told
// a key exists.
bool WriteDefaultIni(const char* path) {
    cameraunlock::IniWriter w;
    if (!w.Open(path)) return false;
    w.WriteComment(" Metro Exodus Enhanced Edition - Head Tracking configuration");
    w.WriteComment(" Lives next to MetroExodus.exe. Edit while the game is closed.");
    WriteGeneralSection(w);
    WriteSensitivitySection(w);
    WriteSmoothingSection(w);
    WritePositionSection(w);
    WriteHotkeysSection(w);
    WriteViewSection(w);
    WriteCameraSection(w);
    WriteLightSection(w);
    w.Close();
    return true;
}

}  // namespace

// GetPrivateProfileString's writer half is the only thing here that can change
// one key of an existing file. A file that is not there yet gets just this
// section, and the next launch fills the rest in.
void Config::SaveAdsMode(AdsMode mode) const {
    if (ini_path.empty()) {
        Log::Line("ERROR: no INI path to save AdsMode to; the setting applies for this session "
                  "but will not survive a restart");
        return;
    }
    if (!WritePrivateProfileStringA("View", "AdsMode", AdsModeValue(mode), ini_path.c_str())) {
        Log::Line("ERROR: could not save AdsMode to %s (error %lu); the setting applies for this "
                  "session but will not survive a restart",
                  ini_path.c_str(), GetLastError());
    }
}

bool Config::LoadOrCreate(const char* iniPath) {
    ini_path = iniPath;
    if (!FileExists(iniPath)) {
        if (!WriteDefaultIni(iniPath)) {
            Log::Line("ERROR: Could not write default INI: %s", iniPath);
            return false;
        }
    }

    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return false;
    }

    if (!ReadGeneralSection(ini, *this)) return false;
    ReadSensitivitySection(ini, *this);
    ReadSmoothingSection(ini, *this);
    ReadPositionSection(ini, *this);
    ReadHotkeysSection(ini, *this);
    ReadViewSection(ini, *this);
    return ReadCameraSection(ini, *this);
}

}  // namespace metroex
