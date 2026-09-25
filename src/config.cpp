#include "config.h"

#include "legacy_config/legacy_config.h"

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/input/key_bindings.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace metroex {

namespace {

namespace cfg = cameraunlock::config;
using cameraunlock::input::FormatKeyBindings;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

constexpr char kFovExpected[] = "0, or an angle from 60 to 120";

// A legacy action's key list: the key the old build bound it to, then the Ctrl+Shift chord
// the old build registered beside it while its chord switch was on.
std::string KeyList(int vk, bool chord, char chordLetter) {
    std::vector<KeyBinding> bindings = {{KeyModifiers::kNone, vk}};
    if (chord) bindings.push_back({KeyModifiers::kCtrl | KeyModifiers::kShift, chordLetter});
    return FormatKeyBindings(bindings);
}

}  // namespace

cfg::CodecParseResult<float> FovCodec::Parse(std::string_view text) const {
    cfg::CodecParseResult<float> read = angle_.Parse(text);
    if (read.ok() && read.value != 0.0f && read.value < kMin) return {0.0f, kFovExpected};
    if (!read.ok()) read.error = kFovExpected;
    return read;
}

std::string FovCodec::Render(float value) const {
    if (value != 0.0f && value < kMin) {
        throw std::invalid_argument("[Camera] FieldOfView " + std::to_string(value) + " is neither 0 nor 60 to 120");
    }
    return angle_.Render(value);
}

cfg::ConfigTable<Config> ConfigTable() {
    using C = cfg::schema::Concept;
    cfg::ConfigTable<Config> table = cfg::HeadTrackingConfigTable<Config>(
        {C::UdpPort, C::EnableOnStartup, C::WorldSpaceYaw, C::RotationEnabled, C::LocalSmoothing,
         C::RemoteSmoothing, C::PositionEnabled, C::PositionLimitX, C::PositionLimitY, C::PositionLimitYDown,
         C::PositionLimitZ, C::PositionLimitZBack, C::ToggleKey, C::CycleTrackingModeKey, C::YawModeKey,
         C::LightFollowsHead, C::LightMultiplier});
    table.Select(C::WorldSpaceYaw).Writable()
        .Select(C::RotationEnabled).Writable()
        .Select(C::PositionEnabled).Writable();
    table.Local("Camera", "FieldOfView", &Config::fov_override, FovCodec{},
                "Field of view in degrees, the number the game's own Field of View slider sets.\n"
                "0 leaves that slider alone; otherwise 60 to 120. The game stops its slider at 75, and\n"
                "its engine holds the picture at 60 in a level whatever the slider says; a value here\n"
                "goes past both. Setting it writes six bytes into the running game's code to take that\n"
                "hold off, and widens the bounds the game's own setter enforces. The setting and its\n"
                "bounds are put back when the game exits. The main menu draws at 60 either way.")
        .Local("Camera", "Discovery", &Config::discovery, cfg::BoolCodec{},
               "true: write the pose the tracker sent, the camera the game published and the camera\n"
               "the engine built the frame from to HeadTracking.log every frame. It writes megabytes\n"
               "an hour; leave it false unless you were asked to turn it on.");
    return table;
}

cfg::ImportResult MapLegacyConfig(legacy::ReadStatus status, const legacy::Config& read, Config& out) {
    switch (status) {
        case legacy::ReadStatus::OpenFailed:
            return cfg::ImportResult::Refused("the file could not be opened");
        case legacy::ReadStatus::PortRefused:
            return cfg::ImportResult::Refused("[General] Port is not a whole number from 1024 to 65535");
        case legacy::ReadStatus::FovRefused:
            return cfg::ImportResult::Refused("[Camera] FieldOfView is neither 0 nor 60 to 120");
        case legacy::ReadStatus::Read:
        case legacy::ReadStatus::Absent:
            break;
    }

    // Every one shipped at identity, so nothing folds into the axis code in head_transform.h.
    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> shaping;
    const auto shape = [&](auto value, auto shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, shaping, dropped);
    };
    shape(read.sens_yaw, legacy::kDefaultSensitivity, "Sensitivity", "Yaw");
    shape(read.sens_pitch, legacy::kDefaultSensitivity, "Sensitivity", "Pitch");
    shape(read.sens_roll, legacy::kDefaultSensitivity, "Sensitivity", "Roll");
    shape(read.invert_yaw, legacy::kDefaultInvert, "Sensitivity", "InvertYaw");
    shape(read.invert_pitch, legacy::kDefaultInvert, "Sensitivity", "InvertPitch");
    shape(read.invert_roll, legacy::kDefaultInvert, "Sensitivity", "InvertRoll");
    shape(read.pos_sens_x, legacy::kDefaultPositionSensitivity, "Position", "SensitivityX");
    shape(read.pos_sens_y, legacy::kDefaultPositionSensitivity, "Position", "SensitivityY");
    shape(read.pos_sens_z, legacy::kDefaultPositionSensitivity, "Position", "SensitivityZ");

    out.enable_on_startup = read.enabled_on_startup;
    out.udp_port = read.udp_port;
    out.world_space_yaw = read.world_space_yaw;
    out.local_smoothing = read.local_smoothing;
    out.position.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.position.remote_smoothing = read.remote_smoothing;

    // [Position] Enabled chose the startup mode and nothing else: the cycle still reached
    // every mode.
    out.rotation_enabled = true;
    out.position_enabled = read.position_enabled;
    out.position.limit_x = read.pos_limit_x;
    out.position.limit_y = read.pos_limit_y;
    out.position.limit_y_down = read.pos_limit_y_down;
    out.position.limit_z = read.pos_limit_z;
    out.position.limit_z_back = read.pos_limit_z_back;

    // The frozen reader never hands back a code outside 0x01-0xFE or a modifier key, so every
    // list formats.
    out.toggle_key_name = KeyList(read.vk_toggle, read.chord_toggle, 'Y');
    out.cycle_tracking_mode_key_name = KeyList(read.vk_cycle_mode, read.chord_cycle_mode, 'G');
    out.yaw_mode_key_name = KeyList(read.vk_yaw_mode, read.chord_yaw_mode, 'H');

    out.fov_override = read.fov_override;
    out.discovery = read.discovery;
    out.light.follows_head = read.light_follows_head;
    out.light.multiplier = read.light_multiplier;

    return status == legacy::ReadStatus::Absent ? cfg::ImportResult::Absent(std::move(dropped), std::move(shaping))
                                                : cfg::ImportResult::Imported(std::move(dropped), std::move(shaping));
}

cfg::LegacyImport<Config> LegacyConfigImport() {
    cfg::LegacyImport<Config> import;
    import.run = [](const cfg::LegacyInput& input, Config& out) {
        legacy::Config read;
        const legacy::ReadStatus status = legacy::Read(input.ansi_path.c_str(), read);
        return MapLegacyConfig(status, read, out);
    };
    import.keys = {
        {"General", "EnableOnStartup"}, {"General", "WorldSpaceYaw"},    {"General", "Port"},
        {"Sensitivity", "Yaw"},         {"Sensitivity", "Pitch"},        {"Sensitivity", "Roll"},
        {"Sensitivity", "InvertYaw"},   {"Sensitivity", "InvertPitch"},  {"Sensitivity", "InvertRoll"},
        {"Smoothing", "LocalSmoothing"}, {"Smoothing", "RemoteSmoothing"},
        {"Position", "Enabled"},
        {"Position", "SensitivityX"},   {"Position", "SensitivityY"},    {"Position", "SensitivityZ"},
        {"Position", "LimitX"},         {"Position", "LimitY"},          {"Position", "LimitYDown"},
        {"Position", "LimitZ"},         {"Position", "LimitZBack"},
        {"Hotkeys", "Toggle"},          {"Hotkeys", "CycleMode"},        {"Hotkeys", "YawMode"},
        {"Hotkeys", "ChordToggle"},     {"Hotkeys", "ChordCycleMode"},   {"Hotkeys", "ChordYawMode"},
        {"Camera", "FieldOfView"},      {"Camera", "Discovery"},
        {"Light", "LightFollowsHead"},  {"Light", "LightMultiplier"},
    };
    return import;
}

cfg::ConfigOwnerOptions<Config> ConfigOwnerOptionsFor(const std::wstring& path) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = path;
    options.table = ConfigTable();
    options.import = LegacyConfigImport();
    options.header.display_name = kGameDisplayName;
    return options;
}

}  // namespace metroex
