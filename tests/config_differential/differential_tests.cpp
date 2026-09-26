// The config differential test. Every input is read three ways:
//
//   oracle     the dev pre-release's reader (oracle/), the newest published build, at a71df8b
//   import     the frozen reader in src/legacy_config/, and the startup code it ran under
//   migration  the config owner on a folder holding the file as MetroExodusHeadTracking.ini:
//              the import into a new CameraUnlock.ini, then the canonical reader and table on
//              the result, and the startup code of this build. Every owner reads a scratch
//              Defaults.ini, never the developer's own.
//
// Comparison 1, oracle against import, finds what a player updating from the dev build sees
// change that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test.
//
// Comparison 2, import against migration, is the proof for the migration: no difference but
// a sensitivity or axis inversion the player set away from its shipped identity value, which
// the import must list as pose shaping and drop (approved change pose_shaping). No default
// moved, so the no-file input has no difference either. Each input migrates three times: with
// Defaults.ini at the built-in values, from a read-only legacy file, and over a Defaults.ini
// that differs from the built-in values on every global row. All three must start the game the
// same way, since the migration writes default only where the imported value is what default
// gives at that start.
//
// After every load the legacy file keeps its bytes, last write time and attributes, and the
// folder holds it and CameraUnlock.ini and nothing else (the legacy file alone after a refused
// import). A second load reads CameraUnlock.ini, imports nothing, gives the same settings and
// changes neither file.
//
// The distinct migrated files are written beside the executable under migrated\, for
// lint-migrated.mjs to run core's canonical config lint over.
//
// Inputs: no file, an empty file, the MetroExodusHeadTracking.ini the dev build shipped (the
// same bytes in its installer ZIP, its Nexus ZIP and its launcher-manifest seed, extracted once
// into inputs/), the oracle's own first-run output, a few hand-written files for the listed
// differences, and core's corpus over the shipped file.
//
// The migration's startup state comes from this build's own code: ParseHotkeys is what
// Hotkeys::Start registers and StartupTrackingMode is what TrackingRuntime::Start sets. The
// oracle's and the import's startup code (OracleHotkeys, ImportHotkeys, FromImport) is copied
// by hand from src/hotkeys.cpp and src/tracking_runtime.cpp at a71df8b and 7cca164. Those
// commits cannot change, so the copies cannot drift from them; they were checked line by line
// against them and have to be read against them again if they are ever edited.

#include "config.h"
#include "hotkeys.h"
#include "legacy_config/legacy_config.h"
#include "oracle/oracle_config.h"
#include "tracking_runtime.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace legacy = metroex::legacy;
namespace oracle = metroex::oracle;
namespace cfg = cameraunlock::config;
using cameraunlock::TrackingMode;
using metroex::Config;
using cameraunlock::config::testing::ChordSwitch;
using cameraunlock::config::testing::GenerateIniMutations;
using cameraunlock::config::testing::IniMutation;
using cameraunlock::config::testing::MutationKey;
using cameraunlock::input::KeyModifiers;

int g_failures = 0;

void Fail(const std::string& input, const std::string& what) {
    if (g_failures < 50) std::printf("FAIL [%s]: %s\n", input.c_str(), what.c_str());
    ++g_failures;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::string Narrow(const std::wstring& path) {
    const int size = WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), 'x');
    WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<size_t>(size) - 1);
    return out;
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + Narrow(path));
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + Narrow(path));
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Every file in the folder, name and bytes, for "the import changed nothing".
std::map<std::wstring, std::string> Snapshot(const std::wstring& dir) {
    std::map<std::wstring, std::string> files;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot list the test folder");
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files[data.cFileName] = ReadBytes(dir + L"\\" + data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return files;
}

void EmptyFolder(const std::wstring& dir) {
    for (const auto& entry : Snapshot(dir)) {
        const std::wstring path = dir + L"\\" + entry.first;
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(path.c_str())) throw std::runtime_error("cannot empty the test folder");
    }
}

std::wstring MakeFolder(const std::wstring& parent, const wchar_t* name) {
    const std::wstring dir = parent + L"\\" + name;
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("cannot create the test folder");
    }
    EmptyFolder(dir);
    return dir;
}

std::set<std::wstring> Names(const std::wstring& dir) {
    std::set<std::wstring> names;
    for (const auto& entry : Snapshot(dir)) names.insert(entry.first);
    return names;
}

// A file's bytes, last write time and attributes, which no load may change.
struct FileState {
    std::string bytes;
    unsigned long long written = 0;
    DWORD attributes = 0;
    bool operator==(const FileState& o) const {
        return bytes == o.bytes && written == o.written && attributes == o.attributes;
    }
    bool operator!=(const FileState& o) const { return !(*this == o); }
};

std::optional<FileState> StateOf(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("cannot read the attributes of " + Narrow(path));
    }
    FileState state;
    state.bytes = ReadBytes(path);
    state.written = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                    data.ftLastWriteTime.dwLowDateTime;
    state.attributes = data.dwFileAttributes;
    return state;
}

bool LogSays(const std::vector<std::string>& log, const std::string& text) {
    for (const std::string& line : log) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Startup state: what each build does with its Config
// ---------------------------------------------------------------------------

enum class Action { Toggle, CycleMode, YawMode, AdsMode };

const char* ActionName(Action a) {
    switch (a) {
        case Action::Toggle: return "toggle";
        case Action::CycleMode: return "cycle mode";
        case Action::YawMode: return "yaw mode";
        case Action::AdsMode: return "ADS mode";
    }
    throw std::logic_error("action");
}

// One key the poller watches for an action, and the modifiers it fires with: none is
// NavGuarded (not while Ctrl and Shift are both held), Ctrl+Shift is ChordGuarded.
struct Registration {
    Action action;
    int vk;
    unsigned modifiers;

    bool operator<(const Registration& o) const {
        return std::tie(action, vk, modifiers) < std::tie(o.action, o.vk, o.modifiers);
    }
    bool operator==(const Registration& o) const {
        return action == o.action && vk == o.vk && modifiers == o.modifiers;
    }
};

constexpr unsigned kNav = static_cast<unsigned>(KeyModifiers::kNone);
constexpr unsigned kChord = static_cast<unsigned>(KeyModifiers::kCtrl | KeyModifiers::kShift);

std::string Describe(const std::vector<Registration>& regs) {
    std::string out;
    for (const Registration& r : regs) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%s:%s0x%02X", out.empty() ? "" : " ", ActionName(r.action),
                      r.modifiers == kChord ? "Ctrl+Shift+" : "", r.vk);
        out += buf;
    }
    return out;
}

// The dev build's Hotkeys::Start (src/hotkeys.cpp at a71df8b), with the poller calls recorded.
std::vector<Registration> OracleHotkeys(const oracle::Config& cfg) {
    std::vector<Registration> regs = {
        {Action::Toggle, cfg.vk_toggle, kNav},
        {Action::CycleMode, cfg.vk_cycle_mode, kNav},
        {Action::YawMode, cfg.vk_yaw_mode, kNav},
        {Action::AdsMode, cfg.vk_ads_mode, kNav},
    };
    if (cfg.chord_toggle) regs.push_back({Action::Toggle, 'Y', kChord});
    if (cfg.chord_cycle_mode) regs.push_back({Action::CycleMode, 'G', kChord});
    if (cfg.chord_yaw_mode) regs.push_back({Action::YawMode, 'H', kChord});
    if (cfg.chord_ads_mode) regs.push_back({Action::AdsMode, 'U', kChord});
    std::sort(regs.begin(), regs.end());
    return regs;
}

// Hotkeys::Start as the build that carries the frozen reader runs it (src/hotkeys.cpp at
// 7cca164), with the poller calls recorded.
std::vector<Registration> ImportHotkeys(const legacy::Config& cfg) {
    std::vector<Registration> regs = {
        {Action::Toggle, cfg.vk_toggle, kNav},
        {Action::CycleMode, cfg.vk_cycle_mode, kNav},
        {Action::YawMode, cfg.vk_yaw_mode, kNav},
    };
    if (cfg.chord_toggle) regs.push_back({Action::Toggle, 'Y', kChord});
    if (cfg.chord_cycle_mode) regs.push_back({Action::CycleMode, 'G', kChord});
    if (cfg.chord_yaw_mode) regs.push_back({Action::YawMode, 'H', kChord});
    std::sort(regs.begin(), regs.end());
    return regs;
}

uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

// Every field the two Configs share, floats bit for bit. The oracle's ADS fields have no
// counterpart; comparison 1 lists them. TrackingRuntime::Start and CameraHook::Initialise read
// these fields and nothing else of the Config in both builds, so equal fields are equal
// startup state.
template <class A, class B>
std::vector<std::string> SharedFieldDifferences(const A& a, const B& b) {
    std::vector<std::string> out;
    const auto check = [&out](bool same, const char* name) {
        if (!same) out.push_back(name);
    };
#define SAME(f) check(a.f == b.f, #f)
#define SAME_BITS(f) check(Bits(a.f) == Bits(b.f), #f)
    SAME(enabled_on_startup);
    SAME(udp_port);
    SAME(world_space_yaw);
    SAME_BITS(sens_yaw);
    SAME_BITS(sens_pitch);
    SAME_BITS(sens_roll);
    SAME(invert_yaw);
    SAME(invert_pitch);
    SAME(invert_roll);
    SAME_BITS(local_smoothing);
    SAME_BITS(remote_smoothing);
    SAME(position_enabled);
    SAME_BITS(pos_sens_x);
    SAME_BITS(pos_sens_y);
    SAME_BITS(pos_sens_z);
    SAME_BITS(pos_limit_x);
    SAME_BITS(pos_limit_y);
    SAME_BITS(pos_limit_y_down);
    SAME_BITS(pos_limit_z);
    SAME_BITS(pos_limit_z_back);
    SAME(vk_toggle);
    SAME(vk_cycle_mode);
    SAME(vk_yaw_mode);
    SAME(chord_toggle);
    SAME(chord_cycle_mode);
    SAME(chord_yaw_mode);
    SAME_BITS(fov_override);
    SAME(light_follows_head);
    SAME_BITS(light_multiplier);
    SAME(discovery);
#undef SAME
#undef SAME_BITS
    return out;
}

// ---------------------------------------------------------------------------
// Comparison 1: the dev build against the frozen reader
// ---------------------------------------------------------------------------

// What a player updating from the dev build sees change, and the commit that made each
// change. The changelog carries the same list.
//
// An input counts towards a difference only when the dev build's reading of it shows that
// difference, and is counted again under the variant it shows. A variant no input shows fails
// the run, which is what catches a listed difference the dev build never had.
struct ListedDifference {
    const char* id;
    const char* commit;
    const char* what;
    std::vector<std::string> variants;
    int seen = 0;
    std::map<std::string, int> seen_by_variant;
};

ListedDifference kComparisonOneDifferences[] = {
    {"ads-mode", "9554397",
     "[View] AdsMode is no longer read: whatever it held (paused unless changed), head tracking "
     "carries on through the sights",
     {"paused", "marker", "tracked"}},
    {"ads-key", "9554397",
     "[Hotkeys] AdsMode and ChordAdsMode are no longer read, and neither the AdsMode key "
     "(Insert unless changed) nor Ctrl+Shift+U cycles an ADS mode",
     {"AdsMode key", "Ctrl+Shift+U"}},
};

ListedDifference& Listed(const char* id) {
    for (ListedDifference& d : kComparisonOneDifferences) {
        if (std::strcmp(d.id, id) == 0) return d;
    }
    throw std::logic_error(id);
}

struct OracleRun {
    bool usable = false;
    oracle::Config cfg;
};

struct ImportRun {
    legacy::ReadStatus status = legacy::ReadStatus::Read;
    legacy::Config cfg;
};

bool ImportUsable(legacy::ReadStatus s) {
    return s == legacy::ReadStatus::Read || s == legacy::ReadStatus::Absent;
}

void CompareOracleWithImport(const std::string& name, const OracleRun& o, const ImportRun& i) {
    if (o.usable != ImportUsable(i.status)) {
        Fail(name, std::string("the dev build ") + (o.usable ? "starts" : "does not start") + ", the import " +
                       (ImportUsable(i.status) ? "starts" : "does not start"));
        return;
    }
    if (!o.usable) return;

    for (const std::string& field : SharedFieldDifferences(o.cfg, i.cfg)) {
        Fail(name, "comparison 1: " + field + " differs from the dev build with no listed reason");
    }

    // Every value the dev build can hold differs from tracking straight through the sights:
    // paused stands tracking down while aiming, and marker and tracked replace the pose with
    // one relative to where the head was when the sights came up (ads_gate.h and
    // TrackingRuntime::SamplePerFrame at a71df8b). So every usable input shows this
    // difference, counted under the value the dev build read; the variants are what make the
    // inputs prove the dev build read the key at all.
    ListedDifference& mode = Listed("ads-mode");
    ++mode.seen;
    ++mode.seen_by_variant[oracle::AdsModeValue(o.cfg.ads_mode)];

    // The dev build's registrations with the listed differences applied give the import's.
    std::vector<Registration> expected;
    std::set<std::string> adsKeys;
    for (const Registration& r : OracleHotkeys(o.cfg)) {
        if (r.action != Action::AdsMode) {
            expected.push_back(r);
        } else {
            adsKeys.insert(r.modifiers == kChord ? "Ctrl+Shift+U" : "AdsMode key");
        }
    }
    if (!adsKeys.empty()) {
        ListedDifference& key = Listed("ads-key");
        ++key.seen;
        for (const std::string& k : adsKeys) ++key.seen_by_variant[k];
    }
    const std::vector<Registration> actual = ImportHotkeys(i.cfg);
    if (expected != actual) {
        Fail(name, "comparison 1: hotkeys " + Describe(actual) + ", the dev build less the listed differences " +
                       Describe(expected));
    }
}

// ---------------------------------------------------------------------------
// The corpus descriptor of every key the frozen reader reads
// ---------------------------------------------------------------------------

std::vector<MutationKey> CorpusKeys() {
    const auto boolean = [](const char* s, const char* k, const char* alt) {
        return MutationKey{s, k, alt, {}, false, {}};
    };
    const auto sens = [](const char* s, const char* k) { return MutationKey{s, k, "0.5", {"150"}, false, {}}; };
    const auto limit = [](const char* k) { return MutationKey{"Position", k, "0.25", {"10.5", "-0.1"}, false, {}}; };
    const auto smooth = [](const char* k) { return MutationKey{"Smoothing", k, "0.3", {"1.5", "-0.5"}, false, {}}; };
    const auto hotkey = [](const char* k, const char* alt, const char* chord) {
        return MutationKey{"Hotkeys", k, alt, {"0x1FF"}, true, {ChordSwitch{"Hotkeys", chord, "1", "0"}}};
    };
    return {
        boolean("General", "EnableOnStartup", "0"),
        boolean("General", "WorldSpaceYaw", "0"),
        MutationKey{"General", "Port", "5000", {"1023", "65536"}, false, {}},
        sens("Sensitivity", "Yaw"),
        sens("Sensitivity", "Pitch"),
        sens("Sensitivity", "Roll"),
        boolean("Sensitivity", "InvertYaw", "1"),
        boolean("Sensitivity", "InvertPitch", "1"),
        boolean("Sensitivity", "InvertRoll", "1"),
        smooth("LocalSmoothing"),
        smooth("RemoteSmoothing"),
        boolean("Position", "Enabled", "0"),
        sens("Position", "SensitivityX"),
        sens("Position", "SensitivityY"),
        sens("Position", "SensitivityZ"),
        limit("LimitX"),
        limit("LimitY"),
        limit("LimitYDown"),
        limit("LimitZ"),
        limit("LimitZBack"),
        hotkey("Toggle", "0x70", "ChordToggle"),
        hotkey("CycleMode", "0x71", "ChordCycleMode"),
        hotkey("YawMode", "0x72", "ChordYawMode"),
        boolean("Hotkeys", "ChordToggle", "0"),
        boolean("Hotkeys", "ChordCycleMode", "0"),
        boolean("Hotkeys", "ChordYawMode", "0"),
        MutationKey{"Camera", "FieldOfView", "90", {"59", "121"}, false, {}},
        boolean("Camera", "Discovery", "1"),
        boolean("Light", "LightFollowsHead", "0"),
        MutationKey{"Light", "LightMultiplier", "2.5", {"5.5", "-1"}, false, {}},
    };
}

// ---------------------------------------------------------------------------
// Comparison 2: the frozen reader against the migration
// ---------------------------------------------------------------------------

// What the mod starts with. Pose shaping is not here: the migrated build applies none, and
// CheckPoseShaping holds the import to listing every value it leaves out.
struct Startup {
    int port = 0;
    bool enabled = false;
    TrackingMode mode = TrackingMode::RotationAndPosition;
    bool world_yaw = false;
    uint32_t local_smoothing = 0;
    uint32_t remote_smoothing = 0;
    uint32_t limit_x = 0;
    uint32_t limit_y = 0;
    uint32_t limit_y_down = 0;
    uint32_t limit_z = 0;
    uint32_t limit_z_back = 0;
    uint32_t fov = 0;
    bool discovery = false;
    bool light_follows_head = false;
    uint32_t light_multiplier = 0;
    std::vector<Registration> hotkeys;
};

// The frozen reader's build: [Position] Enabled chose between the first two modes
// (TrackingRuntime::Start at 7cca164), and CameraHook::Initialise took the field of view, the
// discovery switch and the light from the Config as read.
Startup FromImport(const legacy::Config& c) {
    Startup s;
    s.port = c.udp_port;
    s.enabled = c.enabled_on_startup;
    s.mode = c.position_enabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly;
    s.world_yaw = c.world_space_yaw;
    s.local_smoothing = Bits(c.local_smoothing);
    s.remote_smoothing = Bits(c.remote_smoothing);
    s.limit_x = Bits(c.pos_limit_x);
    s.limit_y = Bits(c.pos_limit_y);
    s.limit_y_down = Bits(c.pos_limit_y_down);
    s.limit_z = Bits(c.pos_limit_z);
    s.limit_z_back = Bits(c.pos_limit_z_back);
    s.fov = Bits(c.fov_override);
    s.discovery = c.discovery;
    s.light_follows_head = c.light_follows_head;
    s.light_multiplier = Bits(c.light_multiplier);
    s.hotkeys = ImportHotkeys(c);
    return s;
}

void AddBindings(std::vector<Registration>& regs, Action action,
                 const std::vector<cameraunlock::input::KeyBinding>& bindings) {
    for (const cameraunlock::input::KeyBinding& b : bindings) {
        regs.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
    }
}

// This build: StartupTrackingMode and ParseHotkeys are the code TrackingRuntime::Start and
// Hotkeys::Start run; the other fields reach TrackingRuntime::Start and CameraHook::Initialise
// as read.
Startup FromMigration(const Config& c) {
    Startup s;
    s.port = c.udp_port;
    s.enabled = c.enable_on_startup;
    s.mode = metroex::StartupTrackingMode(c);
    s.world_yaw = c.world_space_yaw;
    s.local_smoothing = Bits(c.position.local_smoothing);
    s.remote_smoothing = Bits(c.position.remote_smoothing);
    if (Bits(c.local_smoothing) != s.local_smoothing || Bits(c.remote_smoothing) != s.remote_smoothing) {
        throw std::logic_error("the smoothing pair and its copy in position differ");
    }
    s.limit_x = Bits(c.position.limit_x);
    s.limit_y = Bits(c.position.limit_y);
    s.limit_y_down = Bits(c.position.limit_y_down);
    s.limit_z = Bits(c.position.limit_z);
    s.limit_z_back = Bits(c.position.limit_z_back);
    s.fov = Bits(c.fov_override);
    s.discovery = c.discovery;
    s.light_follows_head = c.light.follows_head;
    s.light_multiplier = Bits(c.light.multiplier);
    const metroex::HotkeyBindings bindings = metroex::ParseHotkeys(c);
    AddBindings(s.hotkeys, Action::Toggle, bindings.toggle);
    AddBindings(s.hotkeys, Action::CycleMode, bindings.cycleMode);
    AddBindings(s.hotkeys, Action::YawMode, bindings.yawMode);
    std::sort(s.hotkeys.begin(), s.hotkeys.end());
    return s;
}

std::vector<std::string> StartupDifferences(const Startup& a, const Startup& b) {
    std::vector<std::string> out;
#define SAME(f) \
    if (a.f != b.f) out.push_back(#f)
    SAME(port);
    SAME(enabled);
    SAME(mode);
    SAME(world_yaw);
    SAME(local_smoothing);
    SAME(remote_smoothing);
    SAME(limit_x);
    SAME(limit_y);
    SAME(limit_y_down);
    SAME(limit_z);
    SAME(limit_z_back);
    SAME(fov);
    SAME(discovery);
    SAME(light_follows_head);
    SAME(light_multiplier);
#undef SAME
    if (a.hotkeys != b.hotkeys) out.push_back("hotkeys " + Describe(a.hotkeys) + " against " + Describe(b.hotkeys));
    return out;
}

// The only difference comparison 2 allows: every sensitivity and inversion the frozen reader
// read is listed, folded where it holds the shipped identity value, and dropped as PoseShaping
// where it does not. Nothing else is dropped.
int CheckPoseShaping(const std::string& name, const legacy::Config& c, const cfg::ImportResult& result) {
    struct Read {
        const char* section;
        const char* key;
        bool shipped;
    };
    const Read reads[] = {
        {"Sensitivity", "Yaw", c.sens_yaw == legacy::kDefaultSensitivity},
        {"Sensitivity", "Pitch", c.sens_pitch == legacy::kDefaultSensitivity},
        {"Sensitivity", "Roll", c.sens_roll == legacy::kDefaultSensitivity},
        {"Sensitivity", "InvertYaw", c.invert_yaw == legacy::kDefaultInvert},
        {"Sensitivity", "InvertPitch", c.invert_pitch == legacy::kDefaultInvert},
        {"Sensitivity", "InvertRoll", c.invert_roll == legacy::kDefaultInvert},
        {"Position", "SensitivityX", c.pos_sens_x == legacy::kDefaultPositionSensitivity},
        {"Position", "SensitivityY", c.pos_sens_y == legacy::kDefaultPositionSensitivity},
        {"Position", "SensitivityZ", c.pos_sens_z == legacy::kDefaultPositionSensitivity},
    };
    if (result.pose_shaping.size() != std::size(reads)) {
        Fail(name, "the import lists " + std::to_string(result.pose_shaping.size()) + " pose-shaping values, not 9");
        return 0;
    }
    int dropped = 0;
    for (size_t k = 0; k < std::size(reads); ++k) {
        const cfg::PoseShapingValue& v = result.pose_shaping[k];
        const std::string label = std::string("[") + reads[k].section + "] " + reads[k].key;
        if (v.section != reads[k].section || v.key != reads[k].key) Fail(name, label + " is not listed in its place");
        if (v.folded != reads[k].shipped) Fail(name, label + " is " + (v.folded ? "folded" : "dropped") + " wrongly");
        const bool listed = std::any_of(result.dropped.begin(), result.dropped.end(), [&](const cfg::DroppedValue& d) {
            return d.rule == cfg::DropRule::PoseShaping && d.section == reads[k].section && d.key == reads[k].key;
        });
        if (listed == reads[k].shipped) Fail(name, label + (listed ? " is dropped at its shipped value" : " is changed and not dropped"));
        if (!reads[k].shipped) ++dropped;
    }
    if (static_cast<int>(result.dropped.size()) != dropped) Fail(name, "the import drops a value no approved change names");
    return dropped;
}

struct MigrationTally {
    std::string committed;
    std::set<std::string> migrated;
    int created = 0;
    int converted = 0;
    int refused = 0;
    int with_pose_shaping_dropped = 0;
};

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

struct Folders {
    std::wstring oracle;
    std::wstring import;
    std::wstring migration;
    std::wstring read_only;
    std::wstring skewed;
    // Defaults.ini at the built-in values, which the owner creates on its first load, and one
    // that differs from them on every global row. Both are outside the game folders, whose
    // contents the test holds to the legacy file and CameraUnlock.ini.
    std::wstring defaults;
    std::wstring skewed_defaults;
};

const wchar_t kLegacyName[] = L"MetroExodusHeadTracking.ini";
const wchar_t kConfigName[] = L"CameraUnlock.ini";

// A Defaults.ini holding a value other than the built-in one on every global row the table
// binds.
const char kSkewedDefaults[] =
    "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n"
    "[Network]\r\nUdpPort=5252\r\n\r\n"
    "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\nRotationEnabled=false\r\n\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.5\r\nRemoteSmoothing=0.5\r\n\r\n"
    "[Position]\r\nPositionEnabled=true\r\nPositionLimitX=0.5\r\nPositionLimitY=0.5\r\n"
    "PositionLimitYDown=0.5\r\nPositionLimitZ=0.5\r\nPositionLimitZBack=0.5\r\n\r\n"
    "[Hotkeys]\r\nToggleKey=F8\r\nCycleTrackingModeKey=F9\r\nYawModeKey=F10\r\n\r\n"
    "[Light]\r\nLightFollowsHead=false\r\nLightMultiplier=3.0\r\n";

cfg::ConfigOwnerOptions<Config> Options(const std::wstring& folder, const std::wstring& defaults) {
    return metroex::ConfigOwnerOptionsFor(folder + L"\\" + kConfigName, folder + L"\\" + kLegacyName,
                                          cfg::DefaultsFile::At(defaults));
}

// Runs the owner on `folder`, which holds the input as the legacy file (or nothing), over the
// Defaults.ini at `defaults`, checks what the migration owes beyond comparison 2, and returns
// the startup state it gives, or nothing for a refused file.
std::optional<Startup> MigrateInput(const std::wstring& folder, const std::wstring& defaults, const std::string& name,
                                    const std::optional<std::string>& bytes, const ImportRun& i,
                                    const cfg::ImportResult* result, MigrationTally& tally) {
    using cfg::ConfigLoadStatus;
    const std::wstring legacy = folder + L"\\" + kLegacyName;
    const std::wstring path = folder + L"\\" + kConfigName;
    const std::optional<FileState> legacyBefore = StateOf(legacy);
    const std::set<std::wstring> both{kConfigName, kLegacyName};

    const cfg::ConfigLoadResult<Config> loaded = cfg::ConfigOwner<Config>(Options(folder, defaults)).Load();
    if (StateOf(legacy) != legacyBefore) Fail(name, "the load changed the legacy file's bytes, write time or attributes");

    if (!bytes) {
        ++tally.created;
        if (loaded.status != ConfigLoadStatus::Created) Fail(name, "no file is not Created");
        if (ReadBytes(path) != tally.committed) Fail(name, "the created file is not the committed file");
        if (Names(folder) != std::set<std::wstring>{kConfigName}) Fail(name, "a first start created more than CameraUnlock.ini");
    } else if (!ImportUsable(i.status)) {
        ++tally.refused;
        if (loaded.status != ConfigLoadStatus::LegacyRefused) Fail(name, "a file the import refuses is not LegacyRefused");
        if (Names(folder) != std::set<std::wstring>{kLegacyName}) {
            Fail(name, "a refused import did not leave the legacy file alone in the folder");
        }
        return std::nullopt;
    } else {
        ++tally.converted;
        if (loaded.status != ConfigLoadStatus::Migrated) {
            Fail(name, std::string("the migration is ") + cfg::ConfigLoadStatusName(loaded.status) + ": " + loaded.reason);
            return std::nullopt;
        }
        if (Names(folder) != both) Fail(name, "the folder holds more than the legacy file and CameraUnlock.ini");
        tally.with_pose_shaping_dropped += CheckPoseShaping(name, i.cfg, *result) > 0 ? 1 : 0;
    }

    const std::string migrated = ReadBytes(path);
    if (!cfg::HasCanonicalStamp(migrated)) Fail(name, "CameraUnlock.ini carries no stamp");
    if (!loaded.diagnostics.empty()) Fail(name, "CameraUnlock.ini reads with a diagnostic");
    const Startup started = FromMigration(loaded.config);
    for (const std::string& d : StartupDifferences(FromImport(i.cfg), started)) {
        Fail(name, "comparison 2: " + d);
    }
    tally.migrated.insert(migrated);

    // The next start reads CameraUnlock.ini, imports nothing and writes nothing.
    const std::optional<FileState> created = StateOf(path);
    const std::set<std::wstring> names = Names(folder);
    const cfg::ConfigLoadResult<Config> reread = cfg::ConfigOwner<Config>(Options(folder, defaults)).Load();
    if (reread.status != ConfigLoadStatus::Canonical || !reread.diagnostics.empty()) {
        Fail(name, "the next start does not read CameraUnlock.ini cleanly");
    }
    if (StartupDifferences(started, FromMigration(reread.config)).size() != 0) {
        Fail(name, "the next start gives other settings");
    }
    if (StateOf(path) != created || StateOf(legacy) != legacyBefore || Names(folder) != names) {
        Fail(name, "the next start changed a file");
    }
    if (legacyBefore && !LogSays(reread.log, "is left as it was and is not read")) {
        Fail(name, "the next start does not log that the legacy file is not read");
    }
    return started;
}

void RunInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes,
              MigrationTally& tally) {
    OracleRun o;
    {
        EmptyFolder(f.oracle);
        const std::wstring path = f.oracle + L"\\" + kLegacyName;
        if (bytes) WriteBytes(path, *bytes);
        o.usable = o.cfg.LoadOrCreate(Narrow(path).c_str());
    }

    ImportRun i;
    std::optional<cfg::ImportResult> result;
    {
        EmptyFolder(f.import);
        const std::wstring path = f.import + L"\\" + kLegacyName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (bytes) {
            Config mapped = metroex::ConfigTable().defaults();
            result = metroex::LegacyConfigImport().run(cfg::LegacyInput{path, Narrow(path), false}, mapped);
        }
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (!bytes && i.status != legacy::ReadStatus::Absent) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);

    const cfg::ImportResult* imported = result ? &*result : nullptr;
    EmptyFolder(f.migration);
    if (bytes) WriteBytes(f.migration + L"\\" + kLegacyName, *bytes);
    const std::optional<Startup> writable = MigrateInput(f.migration, f.defaults, name, bytes, i, imported, tally);

    EmptyFolder(f.read_only);
    if (bytes) {
        const std::wstring path = f.read_only + L"\\" + kLegacyName;
        WriteBytes(path, *bytes);
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
    }
    const std::optional<Startup> readOnly =
        MigrateInput(f.read_only, f.defaults, name + " (read-only)", bytes, i, imported, tally);
    if (writable.has_value() != readOnly.has_value() ||
        (writable && !StartupDifferences(*writable, *readOnly).empty())) {
        Fail(name, "a read-only legacy file does not migrate as a writable one does");
    }

    // With no legacy file the settings are Defaults.ini's own, so only an input with a file is
    // held to the import there.
    if (bytes) {
        EmptyFolder(f.skewed);
        WriteBytes(f.skewed + L"\\" + kLegacyName, *bytes);
        const std::optional<Startup> skewed =
            MigrateInput(f.skewed, f.skewed_defaults, name + " (skewed Defaults.ini)", bytes, i, imported, tally);
        if (writable.has_value() != skewed.has_value() ||
            (writable && !StartupDifferences(*writable, *skewed).empty())) {
            Fail(name, "the migration starts differently over a Defaults.ini that differs everywhere");
        }
    }
}

std::string ReadInput(const std::string& file) {
    return ReadBytes(Widen(std::string(METROEX_DIFFERENTIAL_INPUTS) + "/" + file));
}

std::string Replaced(std::string base, const std::string& from, const std::string& to) {
    const size_t at = base.find(from);
    if (at == std::string::npos) throw std::logic_error("no " + from + " in the base file");
    return base.replace(at, from.size(), to);
}

void TestFrozenDefaults() {
    const oracle::Config o;
    const legacy::Config l;
    for (const std::string& field : SharedFieldDifferences(o, l)) {
        Fail("defaults", field + ": the frozen default differs from the dev build's");
    }
}

}  // namespace

int main() {
    try {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        const std::wstring root = std::wstring(temp) + L"metroex-config-differential-" +
                                  std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(root.c_str(), nullptr);
        const std::wstring global = MakeFolder(root, L"global");
        const std::wstring skewedGlobal = MakeFolder(root, L"skewed-global");
        const Folders folders{MakeFolder(root, L"oracle"),    MakeFolder(root, L"import"),
                              MakeFolder(root, L"migration"), MakeFolder(root, L"read-only"),
                              MakeFolder(root, L"skewed"),    global + L"\\Defaults.ini",
                              skewedGlobal + L"\\Defaults.ini"};
        WriteBytes(folders.skewed_defaults, kSkewedDefaults);
        MigrationTally tally;
        tally.committed = ReadBytes(Widen(METROEX_COMMITTED_CONFIG));

        TestFrozenDefaults();

        const std::string shipped = ReadInput("shipped-a71df8b.ini");
        std::string firstRun;
        {
            EmptyFolder(folders.oracle);
            const std::wstring path = folders.oracle + L"\\" + kLegacyName;
            oracle::Config created;
            if (!created.LoadOrCreate(Narrow(path).c_str())) Fail("first run", "the dev build does not start");
            firstRun = ReadBytes(path);
        }

        std::vector<std::pair<std::string, std::optional<std::string>>> inputs = {
            {"no file", std::nullopt},
            {"empty file", std::string()},
            {"shipped by the dev build, and its seed", shipped},
            {"AdsMode=marker", Replaced(shipped, "AdsMode=paused", "AdsMode=marker")},
            {"AdsMode=tracked", Replaced(shipped, "AdsMode=paused", "AdsMode=tracked")},
            {"YawMode on the AdsMode key", Replaced(shipped, "YawMode=0x22", "YawMode=0x2D")},
            {"ChordAdsMode=0", Replaced(shipped, "ChordAdsMode=1", "ChordAdsMode=0")},
            {"FieldOfView=nan", Replaced(shipped, "FieldOfView=0", "FieldOfView=nan")},
        };
        if (firstRun != shipped) inputs.push_back({"first run of the dev build", firstRun});

        for (const auto& input : inputs) RunInput(folders, input.first, input.second, tally);

        // Fresh equals upgrade: the file the dev build shipped and seeded, and the one it wrote
        // on its first run, each as the legacy file, import into the committed file, the file a
        // first start with no legacy file creates. Defaults.ini holds the built-in values, so an
        // untouched row migrates as default.
        for (const auto& upgrade : {std::make_pair("shipped", shipped), std::make_pair("first run", firstRun)}) {
            EmptyFolder(folders.migration);
            WriteBytes(folders.migration + L"\\" + kLegacyName, upgrade.second);
            const cfg::ConfigLoadResult<Config> loaded =
                cfg::ConfigOwner<Config>(Options(folders.migration, folders.defaults)).Load();
            if (loaded.status != cfg::ConfigLoadStatus::Migrated) Fail(upgrade.first, "does not migrate");
            if (ReadBytes(folders.migration + L"\\" + kConfigName) != tally.committed) {
                Fail(upgrade.first, "does not import into the committed file");
            }
        }

        const std::vector<IniMutation> corpus =
            GenerateIniMutations(shipped, metroex::LegacyConfigImport().keys, CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(folders, "corpus: " + m.name, m.bytes, tally);

        std::printf("%zu inputs, %zu of them from the corpus; the dev build's first-run output %s its shipped file\n",
                    inputs.size() + corpus.size(), corpus.size(), firstRun == shipped ? "is" : "is not");
        std::printf("comparison 1, the dev build against the frozen reader:\n");
        for (const ListedDifference& d : kComparisonOneDifferences) {
            std::printf("  %s (%s): shown by %d inputs (", d.id, d.commit, d.seen);
            for (size_t k = 0; k < d.variants.size(); ++k) {
                const auto count = d.seen_by_variant.find(d.variants[k]);
                const int n = count == d.seen_by_variant.end() ? 0 : count->second;
                std::printf("%s%s %d", k == 0 ? "" : ", ", d.variants[k].c_str(), n);
                if (n == 0) Fail(d.id, d.variants[k] + ": a listed variant no input shows");
            }
            std::printf(")\n    %s\n", d.what);
            if (d.seen_by_variant.size() != d.variants.size()) {
                Fail(d.id, "an input shows a variant the list does not name");
            }
        }
        std::printf("comparison 2, the frozen reader against the migration, in loads (no file twice, every other "
                    "input three times): %d created, %d converted (%d with a changed sensitivity or inversion "
                    "dropped), %d refused as the dev build refused them, %zu distinct files\n",
                    tally.created, tally.converted, tally.with_pose_shaping_dropped, tally.refused,
                    tally.migrated.size());
        if (tally.with_pose_shaping_dropped == 0) Fail("pose shaping", "no input drops a changed value");

        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring lintDir(exe);
        lintDir = MakeFolder(lintDir.substr(0, lintDir.find_last_of(L'\\')), L"migrated");
        int n = 0;
        for (const std::string& file : tally.migrated) {
            WriteBytes(lintDir + L"\\" + std::to_wstring(n++) + L".ini", file);
        }

        for (const std::wstring& dir :
             {folders.oracle, folders.import, folders.migration, folders.read_only, folders.skewed, global, skewedGlobal}) {
            EmptyFolder(dir);
            RemoveDirectoryW(dir.c_str());
        }
        RemoveDirectoryW(root.c_str());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("config differential: all passed\n");
        return 0;
    }
    std::printf("config differential: %d failure(s)\n", g_failures);
    return 1;
}
