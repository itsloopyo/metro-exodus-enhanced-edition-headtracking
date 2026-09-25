#pragma once

#include <cstdint>

// The MetroExodusHeadTracking.ini reader as the last build before the canonical config format
// (7cca164) ran it, frozen so an old file converts exactly as that build read it. Never edited:
// a change here changes what a player's old file means.
//
// The defaults and bounds are literals rather than core's constants. They are what core held at
// 26b4f17 (the published dev build's pin) and befb88e, so a later core default cannot move what
// an old file without the key means.

namespace metroex::legacy {

constexpr float kMinFovOverride = 60.0f;
constexpr float kMaxFovOverride = 120.0f;
constexpr int kMinUdpPort = 1024;
constexpr int kMaxUdpPort = 65535;
constexpr float kMaxLightMultiplier = 5.0f;

constexpr bool kDefaultEnableOnStartup = true;
constexpr int kDefaultUdpPort = 4242;
constexpr bool kDefaultWorldSpaceYaw = true;

constexpr float kDefaultSensitivity = 1.0f;
constexpr bool kDefaultInvert = false;

constexpr float kDefaultLocalSmoothing = static_cast<float>(0.0);
constexpr float kDefaultRemoteSmoothing = static_cast<float>(0.15);

constexpr bool kDefaultPositionEnabled = true;
constexpr float kDefaultPositionSensitivity = 1.0f;
constexpr float kDefaultPosLimitX = 0.30f;
constexpr float kDefaultPosLimitY = 0.20f;
constexpr float kDefaultPosLimitYDown = 0.20f;
constexpr float kDefaultPosLimitZ = 0.40f;
constexpr float kDefaultPosLimitZBack = 0.10f;

constexpr int kDefaultVkToggle = 0x23;
constexpr int kDefaultVkCycleMode = 0x21;
constexpr int kDefaultVkYawMode = 0x22;
constexpr bool kDefaultChordEnabled = true;

constexpr float kDefaultFovOverride = 0.0f;
constexpr bool kDefaultDiscovery = false;
constexpr bool kDefaultLightFollowsHead = true;
constexpr float kDefaultLightMultiplier = 1.5f;

struct Config {
    bool enabled_on_startup = kDefaultEnableOnStartup;
    uint16_t udp_port = static_cast<uint16_t>(kDefaultUdpPort);
    bool world_space_yaw = kDefaultWorldSpaceYaw;

    float sens_yaw = kDefaultSensitivity;
    float sens_pitch = kDefaultSensitivity;
    float sens_roll = kDefaultSensitivity;
    bool invert_yaw = kDefaultInvert;
    bool invert_pitch = kDefaultInvert;
    bool invert_roll = kDefaultInvert;

    float local_smoothing = kDefaultLocalSmoothing;
    float remote_smoothing = kDefaultRemoteSmoothing;

    bool position_enabled = kDefaultPositionEnabled;
    float pos_sens_x = kDefaultPositionSensitivity;
    float pos_sens_y = kDefaultPositionSensitivity;
    float pos_sens_z = kDefaultPositionSensitivity;
    float pos_limit_x = kDefaultPosLimitX;
    float pos_limit_y = kDefaultPosLimitY;
    float pos_limit_y_down = kDefaultPosLimitYDown;
    float pos_limit_z = kDefaultPosLimitZ;
    float pos_limit_z_back = kDefaultPosLimitZBack;

    int vk_toggle = kDefaultVkToggle;
    int vk_cycle_mode = kDefaultVkCycleMode;
    int vk_yaw_mode = kDefaultVkYawMode;
    bool chord_toggle = kDefaultChordEnabled;
    bool chord_cycle_mode = kDefaultChordEnabled;
    bool chord_yaw_mode = kDefaultChordEnabled;

    float fov_override = kDefaultFovOverride;
    bool light_follows_head = kDefaultLightFollowsHead;
    float light_multiplier = kDefaultLightMultiplier;
    bool discovery = kDefaultDiscovery;
};

enum class ReadStatus {
    // The file was read into the Config.
    Read,
    // There is no file at the path. The builds wrote one holding the defaults and read that,
    // so the Config holds the defaults.
    Absent,
    // The file could not be opened. The builds did not start.
    OpenFailed,
    // [General] Port is outside kMinUdpPort to kMaxUdpPort. The builds did not start. The
    // Config holds what was read before the port.
    PortRefused,
    // [Camera] FieldOfView is neither 0 nor kMinFovOverride to kMaxFovOverride. The builds did
    // not start. The Config holds what was read before it.
    FovRefused,
};

// Reads the file at the ANSI path through GetPrivateProfileStringA, as the builds did. Writes
// nothing. Logs through cameraunlock::logging as the builds did.
ReadStatus Read(const char* iniPath, Config& cfg);

}  // namespace metroex::legacy
