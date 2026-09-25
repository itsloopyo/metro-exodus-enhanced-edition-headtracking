// The committed config, the launcher seed, the owner's saves and the one codec of the mod's own.
//
// `--render-config <path>` writes the table's defaults, rendered, to <path> and exits without
// running the tests; `pixi run render-config` uses it to rewrite the committed file after a
// change to a row, a comment or a default.

#include "config.h"
#include "test_harness.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    const cfg::ConfigTable<Config> table = metroex::ConfigTable();
    return cfg::RenderCanonical(table, table.defaults(), cfg::RenderHeader{metroex::kGameDisplayName});
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
    fs::path Fresh(const std::string& leaf) {
        const fs::path dir = root_ / leaf;
        fs::create_directories(dir);
        return dir / metroex::kConfigFileName;
    }

private:
    fs::path root_;
};

// Enough base64 to read one JSON string value back. It refuses anything outside the alphabet
// rather than skipping it, so a blob the launcher would choke on fails here.
bool DecodeBase64(const std::string& encoded, std::string& out) {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    out.clear();
    uint32_t accumulator = 0;
    int bits = 0;
    size_t padding = 0;
    for (const char c : encoded) {
        if (c == '=') {
            ++padding;
            continue;
        }
        if (padding != 0 || c == '\0') return false;
        const char* found = std::strchr(kAlphabet, c);
        if (found == nullptr) return false;
        accumulator = (accumulator << 6) | static_cast<uint32_t>(found - kAlphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((accumulator >> bits) & 0xFF));
        }
    }
    return padding <= 2;
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

// The launcher writes the config from the manifest's own base64, so it has to be the committed
// file byte for byte. encode-seed.mjs rewrites it; this is what fails when nobody ran it.
void SeedTest(const std::string& committed) {
    const std::string manifest = ReadBytes(METROEX_LAUNCHER_MANIFEST);
    const std::string field = "\"content_b64\"";
    const size_t at = manifest.find(field);
    Check(at != std::string::npos, "launcher-manifest.json carries a seed");
    if (at == std::string::npos) return;
    const size_t open = manifest.find('"', manifest.find(':', at + field.size()) + 1);
    const size_t close = manifest.find('"', open + 1);
    std::string seed;
    Check(DecodeBase64(manifest.substr(open + 1, close - open - 1), seed), "the seed is base64");
    Check(seed == committed, "the launcher seed is the committed file; run pixi run render-config");
    Check(manifest.find(field, close) == std::string::npos, "launcher-manifest.json carries one seed");
}

void CreatedIsTheCommittedFile(Scratch& scratch, const std::string& committed) {
    const fs::path created = scratch.Fresh("created");
    cfg::ConfigOwner<Config> fresh(metroex::ConfigOwnerOptionsFor(created.wstring()));
    Check(fresh.Load().status == cfg::ConfigLoadStatus::Created, "no file loads as Created");
    Check(ReadBytes(created) == committed, "the created file is the committed file");
}

void SaveTests(Scratch& scratch, const std::string& committed) {
    const fs::path file = scratch.Fresh("saves");
    WriteBytes(file, committed);
    cfg::ConfigOwner<Config> owner(metroex::ConfigOwnerOptionsFor(file.wstring()));
    Check(owner.Load().status == cfg::ConfigLoadStatus::Canonical, "the committed file loads as Canonical");

    std::string before = ReadBytes(file);
    Check(owner.Save([](Config& c) { c.world_space_yaw = false; }).status == cfg::ConfigSaveStatus::Saved,
          "the yaw mode saves");
    std::vector<std::string> changed = ChangedLines(before, ReadBytes(file));
    Check(changed.size() == 1 && changed[0] == "WorldSpaceYaw=true -> WorldSpaceYaw=false",
          "the yaw mode save changes its own line and no other");

    before = ReadBytes(file);
    Check(owner.Save([](Config& c) {
              c.rotation_enabled = true;
              c.position_enabled = false;
          }).status == cfg::ConfigSaveStatus::Saved,
          "rotation only saves");
    changed = ChangedLines(before, ReadBytes(file));
    Check(changed.size() == 1 && changed[0] == "PositionEnabled=true -> PositionEnabled=false",
          "the rotation-only save changes its own line and no other");

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

    cfg::ConfigOwner<Config> restarted(metroex::ConfigOwnerOptionsFor(file.wstring()));
    const cfg::ConfigLoadResult<Config> loaded = restarted.Load();
    Check(!loaded.config.world_space_yaw && !loaded.config.rotation_enabled && loaded.config.position_enabled,
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
        SeedTest(committed);
        CreatedIsTheCommittedFile(scratch, committed);
        SaveTests(scratch, committed);
        FieldOfViewReadsZeroOrSixtyToOneTwenty();
    } catch (const std::exception& e) {
        Check(false, e.what());
    }
    return metroex_test::Report();
}
