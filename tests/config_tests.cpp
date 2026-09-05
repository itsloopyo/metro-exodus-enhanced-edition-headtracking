// The INI contract for the vertical position limits.
//
// The processor clamps y as [-limit_y_down, +limit_y], and limit_y_down carries
// its own default. An INI written before the LimitYDown key existed carries only
// LimitY, so LimitY has to reach both bounds or the player gets asymmetric travel
// with nothing saying why.

#include <cstdio>
#include <string>

#include <windows.h>

#include "config.h"
#include "test_harness.h"

namespace {

using metroex_test::Check;

// These are INI values read back through a float parse, so they either
// round-trip exactly or the parse is wrong; the tolerance only keeps the
// comparison off exact float equality.
constexpr float kTolerance = 1e-6f;

void CheckNear(float actual, float expected, const char* what) {
    metroex_test::CheckNear(actual, expected, kTolerance, what);
}

// IniReader reads through GetPrivateProfileString, which resolves a relative path
// against the Windows directory and caches the file it last read, so every case
// gets an absolute path of its own under TEMP.
std::string WriteIni(const char* tag, const char* body) {
    char temp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp);
    const std::string path = std::string(temp) + "metroexodus_ht_config_" + tag + ".ini";

    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "w");
    if (f == nullptr) {
        Check(false, "the case's INI could not be written");
        std::printf("       path was %s\n", path.c_str());
        return path;
    }
    std::fputs(body, f);
    std::fclose(f);
    return path;
}

void LimitYReachesBothBoundsWhenLimitYDownIsAbsent() {
    metroex::Config raised;
    raised.LoadOrCreate(WriteIni("wide", "[Position]\nLimitY=0.40\n").c_str());
    CheckNear(raised.pos_limit_y, 0.40f, "LimitY=0.40 raises the upward bound");
    CheckNear(raised.pos_limit_y_down, 0.40f,
              "LimitY=0.40 raises the downward bound too, rather than leaving 0.20");

    metroex::Config tightened;
    tightened.LoadOrCreate(WriteIni("tight", "[Position]\nLimitY=0.05\n").c_str());
    CheckNear(tightened.pos_limit_y, 0.05f, "LimitY=0.05 lowers the upward bound");
    CheckNear(tightened.pos_limit_y_down, 0.05f, "LimitY=0.05 lowers the downward bound too");
}

// The ADS mode is the player's choice, made from a key or by hand, and the one
// setting the mod writes back itself. An absent key, a typo and a mode renamed
// since an older release wrote the file all have to land on the default rather
// than on head tracking through the sights that nobody asked for.
void AdsModeDefaultsToPausedAndValidatesTheRest() {
    metroex::Config absent;
    absent.LoadOrCreate(WriteIni("ads_absent", "[Position]\nLimitY=0.20\n").c_str());
    Check(absent.ads_mode == metroex::kDefaultAdsMode, "an absent AdsMode is the default");
    Check(absent.ads_mode == metroex::AdsMode::Paused, "and the default is paused");

    metroex::Config typo;
    typo.LoadOrCreate(WriteIni("ads_typo", "[View]\nAdsMode=sights\n").c_str());
    Check(typo.ads_mode == metroex::AdsMode::Paused, "an unknown AdsMode falls back to paused");

    metroex::Config tracked;
    tracked.LoadOrCreate(WriteIni("ads_tracked", "[View]\nAdsMode=tracked\n").c_str());
    Check(tracked.ads_mode == metroex::AdsMode::Tracked, "a known AdsMode is read");

    metroex::Config marker;
    marker.LoadOrCreate(WriteIni("ads_marker", "[View]\nAdsMode=marker\n").c_str());
    Check(marker.ads_mode == metroex::AdsMode::Marker,
          "including marker, which this game ships because it has no ADS reticle to move");
}

// Pressing the key has to survive a restart, and it must not cost the player the
// rest of their config: the writer changes one key of the existing file.
void SaveAdsModeRoundTripsWithoutLosingTheFile() {
    const std::string path = WriteIni("ads_save", "[View]\nAdsMode=paused\n"
                                                  "[Position]\nLimitY=0.35\n");
    metroex::Config first;
    first.LoadOrCreate(path.c_str());
    first.SaveAdsMode(metroex::AdsMode::Marker);

    metroex::Config reloaded;
    reloaded.LoadOrCreate(path.c_str());
    Check(reloaded.ads_mode == metroex::AdsMode::Marker, "the saved AdsMode is read back");
    CheckNear(reloaded.pos_limit_y, 0.35f, "and the rest of the file survived the write");
}

void TheAdsKeyIsInsertAndTheChordIsOn() {
    metroex::Config cfg;
    cfg.LoadOrCreate(WriteIni("ads_keys", "[Position]\nLimitY=0.20\n").c_str());
    Check(cfg.vk_ads_mode == 0x2D, "the ADS cycle is on Insert");
    Check(cfg.chord_ads_mode, "and its Ctrl+Shift+U chord is on");
    Check(cfg.vk_toggle == 0x23, "the toggle stays on End");
    Check(cfg.vk_cycle_mode == 0x21, "and the tracking-mode cycle stays on Page Up");
}

// The yaw mode is the one view setting with both a key and a config entry, and
// the entry is newer than the first shipped INI. A file written before the key
// existed has to come up horizon-locked rather than on whatever a missing key
// happens to parse as.
void YawModeDefaultsToWorldSpaceAndSitsOnPageDown() {
    metroex::Config absent;
    absent.LoadOrCreate(WriteIni("yaw_absent", "[Position]\nLimitY=0.20\n").c_str());
    Check(absent.world_space_yaw, "an absent WorldSpaceYaw is horizon-locked yaw");
    Check(absent.vk_yaw_mode == 0x22, "the yaw-mode toggle is on Page Down");
    Check(absent.chord_yaw_mode, "and its Ctrl+Shift+H chord is on");

    metroex::Config local;
    local.LoadOrCreate(WriteIni("yaw_local", "[General]\nWorldSpaceYaw=false\n").c_str());
    Check(!local.world_space_yaw, "WorldSpaceYaw=false starts the mod on camera-local yaw");
}

void AnExplicitLimitYDownStillWins() {
    metroex::Config cfg;
    cfg.LoadOrCreate(WriteIni("both", "[Position]\nLimitY=0.40\nLimitYDown=0.05\n").c_str());
    CheckNear(cfg.pos_limit_y, 0.40f, "LimitY is read");
    CheckNear(cfg.pos_limit_y_down, 0.05f, "an explicit LimitYDown overrides the mirrored value");
}

// The field of view is the one key that reaches into the game's own settings,
// so 0 has to mean "leave it alone" and a value the engine would refuse has to
// stop the load rather than be quietly moved to the nearest one the game likes.
void FieldOfViewIsOffByDefaultAndRefusesValuesOutsideItsRange() {
    metroex::Config absent;
    Check(absent.LoadOrCreate(WriteIni("fov_absent", "[Position]\nLimitY=0.20\n").c_str()),
          "a file with no FieldOfView key loads");
    CheckNear(absent.fov_override, 0.0f, "and leaves the game's own field of view alone");

    metroex::Config off;
    Check(off.LoadOrCreate(WriteIni("fov_zero", "[Camera]\nFieldOfView=0\n").c_str()),
          "an explicit 0 loads");
    CheckNear(off.fov_override, 0.0f, "and is still the off switch, not a field of view");

    metroex::Config past;
    Check(past.LoadOrCreate(WriteIni("fov_past", "[Camera]\nFieldOfView=90\n").c_str()),
          "a value past the game's own 75-degree limit loads");
    CheckNear(past.fov_override, 90.0f, "and is kept, because widening that limit is the point");

    metroex::Config high;
    Check(!high.LoadOrCreate(WriteIni("fov_high", "[Camera]\nFieldOfView=200\n").c_str()),
          "a field of view past the engine's own clamp fails the load");

    metroex::Config low;
    Check(!low.LoadOrCreate(WriteIni("fov_low", "[Camera]\nFieldOfView=30\n").c_str()),
          "and so does one below the floor the game's own slider has");
}

// The numbers a player types are the one place raw text becomes a float that
// reaches the camera transform, so each is read through the shared guards rather
// than IniReader's parse-a-prefix readers. All five of these loaded silently
// before: "nan" and an overflowing literal became a NaN view matrix written
// every frame, a decimal comma parsed as 0 and passed every range check, a
// negative travel limit inverted the clamp in PositionProcessor and pinned the
// lean at a fixed offset, and an unpollable key code registered a hotkey that
// could never fire.
// A bool with a trailing comment is the player writing a note to themselves,
// not a malformed value. IniReader::ReadBool compares the WHOLE string and
// GetPrivateProfileString does not strip the comment, so reading through it
// silently returned the default: `Enabled=0 ; no 6dof` left positional tracking
// ON with nothing in the log about the line the player had just edited.
void ABoolWithATrailingCommentIsHonouredRatherThanDropped() {
    metroex::Config commented;
    commented.LoadOrCreate(
        WriteIni("bool_comment", "[Position]\nEnabled=0 ; no 6dof for me\n").c_str());
    Check(!commented.position_enabled,
          "a bool followed by an inline comment reads as the value the player wrote");

    metroex::Config hashed;
    hashed.LoadOrCreate(WriteIni("bool_hash", "[Camera]\nDiscovery=1 # for the bug report\n")
                            .c_str());
    Check(hashed.discovery, "and a hash comment is stripped the same way");

    // Case is not the player's problem either.
    metroex::Config shouty;
    shouty.LoadOrCreate(WriteIni("bool_case", "[General]\nWorldSpaceYaw=FALSE\n").c_str());
    Check(!shouty.world_space_yaw, "and the accepted words are matched case-insensitively");

    // Only a value that is genuinely not one of the words falls back, and the
    // fallback is the default rather than whichever way the parse happened to
    // land.
    metroex::Config nonsense;
    nonsense.LoadOrCreate(WriteIni("bool_junk", "[Position]\nEnabled=maybe\n").c_str());
    Check(nonsense.position_enabled, "a word this reader has no meaning for falls back");
}

// An AdsMode that differs only in case is accepted, so it must not be reported
// as refused - the log line would name the mode the player asked for as the one
// it fell back to.
void AnAdsModeIsMatchedWithoutRegardToCase() {
    metroex::Config mixed;
    mixed.LoadOrCreate(WriteIni("ads_case", "[View]\nAdsMode=Tracked\n").c_str());
    Check(mixed.ads_mode == metroex::AdsMode::Tracked,
          "AdsMode=Tracked is the tracked mode, not a typo");

    metroex::Config commented;
    commented.LoadOrCreate(
        WriteIni("ads_comment", "[View]\nAdsMode=marker ; draw the mark\n").c_str());
    Check(commented.ads_mode == metroex::AdsMode::Marker,
          "and a trailing comment does not turn it back into the default");
}

void MalformedNumbersFallBackInsteadOfReachingTheCamera() {
    metroex::Config notFinite;
    notFinite.LoadOrCreate(WriteIni("guard_nan", "[Sensitivity]\nYaw=nan\n").c_str());
    CheckNear(notFinite.sens_yaw, 1.0f, "a non-finite sensitivity falls back to the default");

    metroex::Config comma;
    comma.LoadOrCreate(WriteIni("guard_comma", "[Smoothing]\nRemoteSmoothing=0,15\n").c_str());
    CheckNear(comma.remote_smoothing, 0.15f,
              "a decimal comma falls back to the key's own default rather than parsing as 0");

    metroex::Config negative;
    negative.LoadOrCreate(WriteIni("guard_limit", "[Position]\nLimitZ=-0.40\n").c_str());
    CheckNear(negative.pos_limit_z, 0.0f,
              "a negative travel limit is clamped rather than inverting the lean clamp");

    metroex::Config unbindable;
    unbindable.LoadOrCreate(WriteIni("guard_vk", "[Hotkeys]\nToggle=0x230\n").c_str());
    Check(unbindable.vk_toggle == 0x23,
          "a key code the OS cannot poll falls back to End rather than binding nothing");

    metroex::Config fov;
    Check(!fov.LoadOrCreate(WriteIni("guard_fov", "[Camera]\nFieldOfView=nan\n").c_str()),
          "and a non-finite field of view fails the load rather than reaching the engine");
}

// The default file the mod writes when there is none, read back through the
// same reader the player's own file goes through. Every default lives in three
// places - the member initialiser, the key this file is written with, and the
// fallback the reader uses when a key is absent - and a value that disagrees
// between them is silent: the file on disk says one thing and a config missing
// the key does another.
void TheGeneratedDefaultFileParsesBackAsTheStructDefaults() {
    char temp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp);
    const std::string path = std::string(temp) + "metroexodus_ht_config_generated.ini";
    DeleteFileA(path.c_str());

    metroex::Config written;
    Check(written.LoadOrCreate(path.c_str()), "a missing file is written and then read back");

    const metroex::Config expected;
    Check(written.enabled_on_startup == expected.enabled_on_startup, "EnableOnStartup round-trips");
    Check(written.udp_port == expected.udp_port, "Port round-trips");
    Check(written.world_space_yaw == expected.world_space_yaw, "WorldSpaceYaw round-trips");
    CheckNear(written.sens_yaw, expected.sens_yaw, "Yaw sensitivity round-trips");
    CheckNear(written.sens_pitch, expected.sens_pitch, "Pitch sensitivity round-trips");
    CheckNear(written.sens_roll, expected.sens_roll, "Roll sensitivity round-trips");
    Check(written.invert_yaw == expected.invert_yaw, "InvertYaw round-trips");
    Check(written.invert_pitch == expected.invert_pitch, "InvertPitch round-trips");
    Check(written.invert_roll == expected.invert_roll, "InvertRoll round-trips");
    CheckNear(written.local_smoothing, expected.local_smoothing, "LocalSmoothing round-trips");
    CheckNear(written.remote_smoothing, expected.remote_smoothing, "RemoteSmoothing round-trips");
    Check(written.position_enabled == expected.position_enabled, "Position Enabled round-trips");
    CheckNear(written.pos_sens_x, expected.pos_sens_x, "SensitivityX round-trips");
    CheckNear(written.pos_sens_y, expected.pos_sens_y, "SensitivityY round-trips");
    CheckNear(written.pos_sens_z, expected.pos_sens_z, "SensitivityZ round-trips");
    CheckNear(written.pos_limit_x, expected.pos_limit_x, "LimitX round-trips");
    CheckNear(written.pos_limit_y, expected.pos_limit_y, "LimitY round-trips");
    CheckNear(written.pos_limit_y_down, expected.pos_limit_y_down, "LimitYDown round-trips");
    CheckNear(written.pos_limit_z, expected.pos_limit_z, "LimitZ round-trips");
    CheckNear(written.pos_limit_z_back, expected.pos_limit_z_back, "LimitZBack round-trips");
    Check(written.vk_toggle == expected.vk_toggle, "the toggle key round-trips");
    Check(written.vk_cycle_mode == expected.vk_cycle_mode, "the mode cycle key round-trips");
    Check(written.vk_yaw_mode == expected.vk_yaw_mode, "the yaw mode key round-trips");
    Check(written.vk_ads_mode == expected.vk_ads_mode, "the ADS cycle key round-trips");
    Check(written.chord_toggle == expected.chord_toggle, "ChordToggle round-trips");
    Check(written.chord_cycle_mode == expected.chord_cycle_mode, "ChordCycleMode round-trips");
    Check(written.chord_yaw_mode == expected.chord_yaw_mode, "ChordYawMode round-trips");
    Check(written.chord_ads_mode == expected.chord_ads_mode, "ChordAdsMode round-trips");
    Check(written.ads_mode == expected.ads_mode, "AdsMode round-trips");
    CheckNear(written.fov_override, expected.fov_override, "FieldOfView round-trips");
    Check(written.discovery == expected.discovery, "Discovery round-trips");
}

// The port is the mod's one network-facing setting, and the check on it is fatal
// rather than clamped: a mod that quietly listened on a port other than the one
// the file names looks exactly like a tracker that never connected. So both ends
// of the accepted range have to load and both rejections have to stop the load,
// including the two shapes a hand-edited file actually carries - a 0 left behind
// by "turn it off", and a 65536 written by counting from one.
void ThePortIsHeldToTheUnprivilegedRange() {
    metroex::Config low;
    Check(low.LoadOrCreate(WriteIni("port_low", "[General]\nPort=1024\n").c_str()),
          "the bottom of the unprivileged range loads");
    Check(low.udp_port == 1024, "and is the port the mod listens on");

    metroex::Config high;
    Check(high.LoadOrCreate(WriteIni("port_high", "[General]\nPort=65535\n").c_str()),
          "the top of the range loads");
    Check(high.udp_port == 65535, "and is the port the mod listens on");

    metroex::Config zero;
    Check(!zero.LoadOrCreate(WriteIni("port_zero", "[General]\nPort=0\n").c_str()),
          "port 0 fails the load rather than binding an ephemeral port nothing sends to");

    metroex::Config privileged;
    Check(!privileged.LoadOrCreate(WriteIni("port_priv", "[General]\nPort=1023\n").c_str()),
          "a privileged port fails the load rather than binding nothing without saying so");

    metroex::Config past;
    Check(!past.LoadOrCreate(WriteIni("port_past", "[General]\nPort=65536\n").c_str()),
          "a port past 65535 fails the load rather than wrapping to 0 in the uint16 it is "
          "stored in");

    metroex::Config negative;
    Check(!negative.LoadOrCreate(WriteIni("port_neg", "[General]\nPort=-1\n").c_str()),
          "and so does a negative one");
}

// A path the mod could not resolve has to fail the load rather than be handed to
// the reader as-is. Every read and write here goes through
// GetPrivateProfileString, which resolves a RELATIVE path against the Windows
// directory - so a bare filename does not fail, it silently reads and writes
// the player's settings somewhere they will never find them.
void AnUnresolvableIniPathFailsTheLoad() {
    metroex::Config cfg;
    Check(!cfg.LoadOrCreate(""),
          "an unresolved INI path fails the load rather than resolving against the Windows "
          "directory");
}

}  // namespace

int main() {
    LimitYReachesBothBoundsWhenLimitYDownIsAbsent();
    FieldOfViewIsOffByDefaultAndRefusesValuesOutsideItsRange();
    AnExplicitLimitYDownStillWins();
    ABoolWithATrailingCommentIsHonouredRatherThanDropped();
    AnAdsModeIsMatchedWithoutRegardToCase();
    MalformedNumbersFallBackInsteadOfReachingTheCamera();
    AdsModeDefaultsToPausedAndValidatesTheRest();
    SaveAdsModeRoundTripsWithoutLosingTheFile();
    TheAdsKeyIsInsertAndTheChordIsOn();
    YawModeDefaultsToWorldSpaceAndSitsOnPageDown();
    TheGeneratedDefaultFileParsesBackAsTheStructDefaults();
    ThePortIsHeldToTheUnprivilegedRange();
    AnUnresolvableIniPathFailsTheLoad();

    return metroex_test::Report();
}
