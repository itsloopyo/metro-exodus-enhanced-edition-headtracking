#pragma once

#include <cstdint>
#include <string>

#include "ads.h"

#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace metroex {

// What the FieldOfView key accepts. The floor is the game's own, because the
// slider has never gone below it and the engine scales the HUD by 60 divided by
// the base field of view, so a narrower one draws a larger HUD. The ceiling is
// well short of the 179 degrees the engine hard-clamps the camera to, which
// leaves room for the wide-angle cameras the game uses in vehicles and
// cutscenes to stay under that clamp rather than flattening against it.
constexpr float kMinFovOverride = 60.0f;
constexpr float kMaxFovOverride = 120.0f;

// The port the tracker sends to. 1024 is where the unprivileged range starts.
constexpr int kMinUdpPort = 1024;
constexpr int kMaxUdpPort = 65535;

// Every INI default, in one place. Each of these is read three times - the
// member initialiser below, the key the default file is written with, and the
// fallback the reader uses when the key is absent - and a value that disagrees
// between the three is silent: the file says one thing, a config missing the key
// does another.
namespace defaults {

constexpr bool kEnableOnStartup = true;
constexpr int kUdpPort = 4242;
constexpr bool kWorldSpaceYaw = true;

constexpr float kSensitivity = 1.0f;
constexpr bool kInvert = false;

constexpr bool kPositionEnabled = true;
constexpr float kPositionSensitivity = 1.0f;

constexpr int kVkToggle = 0x23;      // VK_END
constexpr int kVkCycleMode = 0x21;   // VK_PRIOR (Page Up)
constexpr int kVkYawMode = 0x22;     // VK_NEXT (Page Down)
constexpr int kVkAdsMode = 0x2D;     // VK_INSERT
constexpr bool kChordEnabled = true;

// 0 is the off switch, not a field of view.
constexpr float kFovOverride = 0.0f;
constexpr bool kDiscovery = false;
constexpr bool kLightFollowsHead = true;
constexpr float kLightMultiplier = cameraunlock::effects::kDefaultLightMultiplier;

}  // namespace defaults

struct Config {
    bool enabled_on_startup = defaults::kEnableOnStartup;
    uint16_t udp_port = static_cast<uint16_t>(defaults::kUdpPort);

    // Which up-axis head yaw turns about. True keeps it on the world up-axis,
    // so the horizon stays where it is however far the mouse has pitched the
    // camera; false turns about the camera's own up-axis, which leans the view
    // once the camera is pitched steeply. Toggled at runtime; the value here is
    // only what the mod starts on.
    bool world_space_yaw = defaults::kWorldSpaceYaw;

    float sens_yaw = defaults::kSensitivity;
    float sens_pitch = defaults::kSensitivity;
    float sens_roll = defaults::kSensitivity;
    bool invert_yaw = defaults::kInvert;
    bool invert_pitch = defaults::kInvert;
    bool invert_roll = defaults::kInvert;

    // Smoothing is picked per connection from the packet source address: a
    // tracker running on this machine (loopback) uses local_smoothing, a
    // remote network device uses remote_smoothing.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    bool position_enabled = defaults::kPositionEnabled;
    float pos_sens_x = defaults::kPositionSensitivity;
    float pos_sens_y = defaults::kPositionSensitivity;
    float pos_sens_z = defaults::kPositionSensitivity;
    float pos_limit_x = cameraunlock::PositionSettings{}.limit_x;
    float pos_limit_y = cameraunlock::PositionSettings{}.limit_y;
    float pos_limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
    float pos_limit_z = cameraunlock::PositionSettings{}.limit_z;
    float pos_limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    int vk_toggle = defaults::kVkToggle;
    int vk_cycle_mode = defaults::kVkCycleMode;
    int vk_yaw_mode = defaults::kVkYawMode;
    int vk_ads_mode = defaults::kVkAdsMode;
    bool chord_toggle = defaults::kChordEnabled;
    bool chord_cycle_mode = defaults::kChordEnabled;
    bool chord_yaw_mode = defaults::kChordEnabled;
    bool chord_ads_mode = defaults::kChordEnabled;

    // What head tracking does while the sights are up. The cycle, its value
    // strings and its default live in cameraunlock-core; see ads.h for why this
    // game gets all three slots.
    AdsMode ads_mode = kDefaultAdsMode;

    // Field of view in degrees, or 0 to leave the game's own setting alone.
    //
    // The game's Field of View slider stops at 75 because the console variable
    // behind it carries that bound and its setter rejects anything past it. A
    // non-zero value here widens the bound and writes the setting, so it is the
    // same number the slider sets and it reaches the whole engine rather than
    // only the picture. See fov.h.
    float fov_override = defaults::kFovOverride;

    // Whether the torch turns with the head, and how far it leads the view.
    // The 1.5x default and the reasoning for it are core's, not this mod's -
    // see cameraunlock/effects/head_follow_light.h. A value outside 0 to 5 is
    // refused rather than clamped, and the beam stays on the aim.
    bool light_follows_head = defaults::kLightFollowsHead;
    float light_multiplier = defaults::kLightMultiplier;

    // Constant-buffer content logging. It exists for re-deriving the camera
    // buffer layout if a game patch ever changes it, and costs megabytes of log
    // an hour, so it is opt-in.
    bool discovery = defaults::kDiscovery;

    // Where LoadOrCreate() read this config from. SaveAdsMode() writes back to
    // it, so the player's ADS choice survives a restart.
    std::string ini_path;

    bool LoadOrCreate(const char* iniPath);

    // Writes just AdsMode back, leaving every other key and every comment in the
    // file alone. IniWriter truncates, so writing through it would throw the
    // rest of the player's config away.
    void SaveAdsMode(AdsMode mode) const;
};

}
