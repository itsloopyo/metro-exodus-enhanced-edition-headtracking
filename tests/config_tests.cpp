// The committed config, the file a first start creates, the owner's saves and the one codec of
// the mod's own.
//
// `--render-config <path>` writes the table's fresh render to <path> and exits without running
// the tests; `pixi run render-config` uses it to rewrite the committed file after a change to a
// row, a comment or a default.

#include "config.h"
#include "test_harness.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace cfg = cameraunlock::config;
using metroex::Config;
using metroex_test::Check;

namespace {

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

std::string Rendered() {
    return cfg::RenderCanonicalFresh(metroex::ConfigTable(), cfg::RenderHeader{metroex::kGameDisplayName});
}

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i + 1 < bytes.size(); ++i) {
        if (bytes[i] == '\r' && bytes[i + 1] == '\n') {
            lines.push_back(bytes.substr(start, i - start));
            start = i + 2;
        }
    }
    lines.push_back(bytes.substr(start));
    return lines;
}

// Every line that differs, as "before -> after". The two files must have the same number of
// lines.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before), b = Lines(after);
    if (a.size() != b.size()) return {"line count changed"};
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(a[i] + " -> " + b[i]);
    }
    return changed;
}

class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() / ("metroex-config-tests-" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root_);
        fs::create_directories(root_);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    // An empty game folder.
    fs::path Folder(const std::string& leaf) {
        const fs::path dir = root_ / leaf;
        fs::create_directories(dir);
        return dir;
    }
    // A Defaults.ini path of the folder's own, outside it.
    fs::path Defaults(const std::string& leaf) { return root_ / (leaf + "-global") / "Defaults.ini"; }

private:
    fs::path root_;
};

cfg::ConfigOwnerOptions<Config> Options(const fs::path& folder, const fs::path& defaults) {
    return metroex::ConfigOwnerOptionsFor((folder / metroex::kConfigFileName).wstring(),
                                          (folder / metroex::kLegacyConfigFileName).wstring(),
                                          cfg::DefaultsFile::At(defaults.wstring()));
}

std::set<std::string> Names(const fs::path& dir) {
    std::set<std::string> names;
    for (const auto& entry : fs::directory_iterator(dir)) names.insert(entry.path().filename().string());
    return names;
}

bool LogSays(const std::vector<std::string>& log, const std::string& text) {
    for (const std::string& line : log) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

void RenderTest(const std::string& committed) {
    Check(Rendered() == committed,
          "MetroExodusHeadTracking.ini differs from the table's defaults; run pixi run render-config");
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(committed);
    Check(doc.IsReadable() && doc.diagnostics.empty(), "the committed file draws no reader diagnostics");
    Config read;
    Check(cfg::ApplyCanonical(doc, metroex::ConfigTable(), read).diagnostics.empty(),
          "the committed file draws no table diagnostics");
}

void CreatedIsTheCommittedFile(Scratch& scratch, const std::string& committed) {
    const fs::path folder = scratch.Folder("created");
    const fs::path defaults = scratch.Defaults("created");
    const cfg::ConfigLoadResult<Config> loaded = cfg::ConfigOwner<Config>(Options(folder, defaults)).Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Created, "no file loads as Created");
    Check(ReadBytes(folder / metroex::kConfigFileName) == committed,
          "the created CameraUnlock.ini is the committed file");
    Check(Names(folder) == std::set<std::string>{"CameraUnlock.ini"},
          "a first start with no legacy file creates CameraUnlock.ini and nothing else beside it");
    Check(fs::exists(defaults), "a first start creates Defaults.ini where the options name it");
}

// Every save starts from the committed file, whose global rows hold default. A save writes the
// value in place of default, changes the lines of its own rows and no other byte, and touches
// neither Defaults.ini nor the legacy file beside CameraUnlock.ini.
void SaveTests(Scratch& scratch, const std::string& committed) {
    const fs::path folder = scratch.Folder("saves");
    const fs::path defaults = scratch.Defaults("saves");
    const fs::path file = folder / metroex::kConfigFileName;
    const fs::path legacy = folder / metroex::kLegacyConfigFileName;
    const std::string legacyBytes = "[General]\r\nWorldSpaceYaw=0\r\n";
    WriteBytes(file, committed);
    WriteBytes(legacy, legacyBytes);
    cfg::ConfigOwner<Config> owner(Options(folder, defaults));
    const cfg::ConfigLoadResult<Config> first = owner.Load();
    Check(first.status == cfg::ConfigLoadStatus::Canonical, "the committed file loads as Canonical");
    Check(first.config.world_space_yaw, "CameraUnlock.ini is read and the legacy file is not");
    Check(LogSays(first.log, "is left as it was and is not read"),
          "the load logs that the legacy file beside CameraUnlock.ini is not read");
    const std::string defaultsBytes = ReadBytes(defaults);

    std::string before = ReadBytes(file);
    const cfg::ConfigSaveResult saved = owner.Save([](Config& c) { c.world_space_yaw = false; });
    Check(saved.status == cfg::ConfigSaveStatus::Saved, "the yaw mode saves");
    Check(LogSays(saved.log, "WorldSpaceYaw=false is now set for this game, and no longer follows Defaults.ini."),
          "the yaw mode save logs that its row stopped following Defaults.ini");
    std::vector<std::string> changed = ChangedLines(before, ReadBytes(file));
    Check(changed.size() == 1 && changed[0] == "WorldSpaceYaw=default -> WorldSpaceYaw=false",
          "the yaw mode save writes its value over default and changes no other line");

    before = ReadBytes(file);
    Check(owner.Save([](Config& c) {
              c.rotation_enabled = true;
              c.position_enabled = false;
          }).status == cfg::ConfigSaveStatus::Saved,
          "rotation only saves");
    changed = ChangedLines(before, ReadBytes(file));
    Check(changed.size() == 2 && changed[0] == "RotationEnabled=default -> RotationEnabled=true" &&
              changed[1] == "PositionEnabled=default -> PositionEnabled=false",
          "the rotation-only save writes both rows of the pair over default and changes no other line");

    before = ReadBytes(file);
    Check(owner.Save([](Config& c) {
              c.rotation_enabled = false;
              c.position_enabled = true;
          }).status == cfg::ConfigSaveStatus::Saved,
          "position only saves");
    changed = ChangedLines(before, ReadBytes(file));
    Check(changed.size() == 2 && changed[0] == "RotationEnabled=true -> RotationEnabled=false" &&
              changed[1] == "PositionEnabled=false -> PositionEnabled=true",
          "the position-only save changes exactly the pair");

    before = ReadBytes(file);
    bool threw = false;
    try {
        owner.Save([](Config& c) { c.enable_on_startup = false; });
    } catch (const std::logic_error&) {
        threw = true;
    }
    Check(threw, "EnableOnStartup is not Writable: End never persists");
    Check(ReadBytes(file) == before, "a refused save writes nothing");
    Check(ReadBytes(defaults) == defaultsBytes, "no save writes Defaults.ini");
    Check(ReadBytes(legacy) == legacyBytes, "no save writes the legacy file");
    Check(Names(folder) == std::set<std::string>{"CameraUnlock.ini", "MetroExodusHeadTracking.ini"},
          "the saves leave no other file beside CameraUnlock.ini");

    const cfg::ConfigLoadResult<Config> loaded = cfg::ConfigOwner<Config>(Options(folder, defaults)).Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Canonical && !loaded.config.world_space_yaw &&
              !loaded.config.rotation_enabled && loaded.config.position_enabled,
          "the saved toggles come back at the next load");
}

// FieldOfView is 0 or 60 to 120. Anything else keeps the default, 0, which leaves the game's own
// field of view alone.
void FieldOfViewReadsZeroOrSixtyToOneTwenty() {
    const metroex::FovCodec codec;
    Check(codec.Parse("0").ok() && codec.Parse("0").value == 0.0f, "FieldOfView=0 reads");
    Check(codec.Parse("60").ok() && codec.Parse("60").value == 60.0f, "FieldOfView=60 reads");
    Check(codec.Parse("120.0").ok() && codec.Parse("120.0").value == 120.0f, "FieldOfView=120.0 reads");
    Check(!codec.Parse("59.9").ok(), "FieldOfView=59.9 does not read");
    Check(!codec.Parse("30").ok(), "FieldOfView=30 does not read");
    Check(!codec.Parse("120.5").ok(), "FieldOfView=120.5 does not read");
    Check(!codec.Parse("-60").ok(), "FieldOfView=-60 does not read");
    Check(!codec.Parse("nan").ok(), "FieldOfView=nan does not read");
    Check(codec.Render(90.0f) == "90.0" && codec.Render(0.0f) == "0.0", "FieldOfView writes as a float");

    Config read;
    const cfg::ApplyReport report =
        cfg::ApplyCanonical(cfg::ParseCanonicalIni("[CameraUnlock]\r\nConfigFormat=1\r\n[Camera]\r\nFieldOfView=30\r\n"),
                            metroex::ConfigTable(), read);
    Check(read.fov_override == 0.0f && report.diagnostics.size() == 1 &&
              report.diagnostics[0].kind == cfg::CanonicalDiagnosticKind::InvalidValue,
          "a FieldOfView outside its range keeps 0 and is reported");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
            WriteBytes(argv[2], Rendered());
            std::printf("wrote %s\n", argv[2]);
            return 0;
        }
        if (argc != 1) {
            std::printf("usage: %s [--render-config <path>]\n", argv[0]);
            return 2;
        }

        const std::string committed = ReadBytes(METROEX_COMMITTED_CONFIG);
        Scratch scratch;
        RenderTest(committed);
        CreatedIsTheCommittedFile(scratch, committed);
        SaveTests(scratch, committed);
        FieldOfViewReadsZeroOrSixtyToOneTwenty();
    } catch (const std::exception& e) {
        Check(false, e.what());
    }
    return metroex_test::Report();
}
