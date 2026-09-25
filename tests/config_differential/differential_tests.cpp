// The config differential test. Every input is read two ways:
//
//   oracle  the dev pre-release's reader (oracle/), the newest published build, at a71df8b
//   import  the frozen reader in src/legacy_config/, and the startup code it runs under
//
// Comparison 1, oracle against import, finds what a player updating from the dev build sees
// change that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test.
//
// Inputs: no file, an empty file, the MetroExodusHeadTracking.ini the dev build shipped (the
// same bytes in its installer ZIP, its Nexus ZIP and its launcher-manifest seed, extracted once
// into inputs/), the oracle's own first-run output, a few hand-written files for the listed
// differences, and core's corpus over the shipped file.

#include "legacy_config/legacy_config.h"
#include "oracle/oracle_config.h"

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace legacy = metroex::legacy;
namespace oracle = metroex::oracle;
using cameraunlock::config::LegacyKey;
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
struct ListedDifference {
    const char* id;
    const char* commit;
    const char* what;
    int seen = 0;
};

ListedDifference kComparisonOneDifferences[] = {
    {"ads-mode", "9554397",
     "[View] AdsMode is no longer read: whatever it held (paused unless changed), head tracking "
     "carries on through the sights and the lean eases out while they are up"},
    {"ads-key", "9554397",
     "[Hotkeys] AdsMode and ChordAdsMode are no longer read, and neither the AdsMode key "
     "(Insert unless changed) nor Ctrl+Shift+U cycles an ADS mode"},
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

    ++Listed("ads-mode").seen;
    ++Listed("ads-key").seen;

    // The dev build's registrations with the listed differences applied give the import's.
    std::vector<Registration> expected;
    for (const Registration& r : OracleHotkeys(o.cfg)) {
        if (r.action != Action::AdsMode) expected.push_back(r);
    }
    const std::vector<Registration> actual = ImportHotkeys(i.cfg);
    if (expected != actual) {
        Fail(name, "comparison 1: hotkeys " + Describe(actual) + ", the dev build less the listed differences " +
                       Describe(expected));
    }
}

// ---------------------------------------------------------------------------
// The keys the frozen reader reads, and the corpus descriptors
// ---------------------------------------------------------------------------

std::vector<LegacyKey> FrozenReaderKeys() {
    return {
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
}

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
// The run
// ---------------------------------------------------------------------------

struct Folders {
    std::wstring oracle;
    std::wstring import;
};

const wchar_t kIniName[] = L"MetroExodusHeadTracking.ini";

void RunInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes) {
    OracleRun o;
    {
        EmptyFolder(f.oracle);
        const std::wstring path = f.oracle + L"\\" + kIniName;
        if (bytes) WriteBytes(path, *bytes);
        o.usable = o.cfg.LoadOrCreate(Narrow(path).c_str());
    }

    ImportRun i;
    {
        EmptyFolder(f.import);
        const std::wstring path = f.import + L"\\" + kIniName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (!bytes && i.status != legacy::ReadStatus::Absent) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);
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
        const Folders folders{MakeFolder(root, L"oracle"), MakeFolder(root, L"import")};

        TestFrozenDefaults();

        const std::string shipped = ReadInput("shipped-a71df8b.ini");
        std::string firstRun;
        {
            EmptyFolder(folders.oracle);
            const std::wstring path = folders.oracle + L"\\" + kIniName;
            oracle::Config created;
            if (!created.LoadOrCreate(Narrow(path).c_str())) Fail("first run", "the dev build does not start");
            firstRun = ReadBytes(path);
        }

        std::vector<std::pair<std::string, std::optional<std::string>>> inputs = {
            {"no file", std::nullopt},
            {"empty file", std::string()},
            {"shipped by the dev build, and its seed", shipped},
            {"AdsMode=tracked", Replaced(shipped, "AdsMode=paused", "AdsMode=tracked")},
            {"YawMode on the AdsMode key", Replaced(shipped, "YawMode=0x22", "YawMode=0x2D")},
            {"ChordAdsMode=0", Replaced(shipped, "ChordAdsMode=1", "ChordAdsMode=0")},
            {"FieldOfView=nan", Replaced(shipped, "FieldOfView=0", "FieldOfView=nan")},
        };
        if (firstRun != shipped) inputs.push_back({"first run of the dev build", firstRun});

        for (const auto& input : inputs) RunInput(folders, input.first, input.second);

        const std::vector<IniMutation> corpus = GenerateIniMutations(shipped, FrozenReaderKeys(), CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(folders, "corpus: " + m.name, m.bytes);

        std::printf("%zu inputs, %zu of them from the corpus; the dev build's first-run output %s its shipped file\n",
                    inputs.size() + corpus.size(), corpus.size(), firstRun == shipped ? "is" : "is not");
        std::printf("comparison 1, the dev build against the frozen reader:\n");
        for (const ListedDifference& d : kComparisonOneDifferences) {
            std::printf("  %s (%s): %d inputs\n    %s\n", d.id, d.commit, d.seen, d.what);
            if (d.seen == 0) Fail(d.id, "a listed difference no input shows");
        }

        for (const std::wstring& dir : {folders.oracle, folders.import}) {
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
