#include "config.h"

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

#include <windows.h>

namespace metroex {

namespace {

bool FileExists(const char* path) {
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
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
    w.WriteComment(" Chord alternatives: Ctrl+Shift+Y (toggle tracking),");
    w.WriteComment(" Ctrl+Shift+G (cycle mode), Ctrl+Shift+H (yaw mode).");
    w.WriteBool("ChordToggle", defaults::kChordEnabled);
    w.WriteBool("ChordCycleMode", defaults::kChordEnabled);
    w.WriteBool("ChordYawMode", defaults::kChordEnabled);
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
    WriteCameraSection(w);
    WriteLightSection(w);
    w.Close();
    return true;
}

}  // namespace

bool Config::LoadOrCreate(const char* iniPath) {
    if (!FileExists(iniPath)) {
        if (!WriteDefaultIni(iniPath)) {
            Log::Line("ERROR: Could not write default INI: %s", iniPath);
            return false;
        }
    }

    legacy::Config read;
    const legacy::ReadStatus status = legacy::Read(iniPath, read);
    if (status != legacy::ReadStatus::Read) return false;

    enabled_on_startup = read.enabled_on_startup;
    udp_port = read.udp_port;
    world_space_yaw = read.world_space_yaw;
    sens_yaw = read.sens_yaw;
    sens_pitch = read.sens_pitch;
    sens_roll = read.sens_roll;
    invert_yaw = read.invert_yaw;
    invert_pitch = read.invert_pitch;
    invert_roll = read.invert_roll;
    local_smoothing = read.local_smoothing;
    remote_smoothing = read.remote_smoothing;
    position_enabled = read.position_enabled;
    pos_sens_x = read.pos_sens_x;
    pos_sens_y = read.pos_sens_y;
    pos_sens_z = read.pos_sens_z;
    pos_limit_x = read.pos_limit_x;
    pos_limit_y = read.pos_limit_y;
    pos_limit_y_down = read.pos_limit_y_down;
    pos_limit_z = read.pos_limit_z;
    pos_limit_z_back = read.pos_limit_z_back;
    vk_toggle = read.vk_toggle;
    vk_cycle_mode = read.vk_cycle_mode;
    vk_yaw_mode = read.vk_yaw_mode;
    chord_toggle = read.chord_toggle;
    chord_cycle_mode = read.chord_cycle_mode;
    chord_yaw_mode = read.chord_yaw_mode;
    fov_override = read.fov_override;
    light_follows_head = read.light_follows_head;
    light_multiplier = read.light_multiplier;
    discovery = read.discovery;
    return true;
}

}  // namespace metroex
