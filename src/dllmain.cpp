#include "build_profile.h"
#include "camera_hook.h"
#include "config.h"
#include "hotkeys.h"
#include "logging.h"
#include "path_utils.h"
#include "tracking_runtime.h"
#include "window_centering.h"

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

namespace metroex {

namespace {

// Nothing unloads this module.
//
// The render thread can be anywhere inside the camera detour at the moment a
// FreeLibrary lands, and MinHook returns the trampoline to its pool as soon as a
// hook is removed - so an unload races a call through recycled memory that no
// amount of null-checking closes. Pinning removes the race by removing the
// unload: DLL_PROCESS_DETACH is then reachable only at process exit, where every
// other thread has already stopped.
//
// Done here rather than in DllMain because GetModuleHandleEx walks the loader,
// and DLL_PROCESS_ATTACH already holds the loader lock.
//
// A failed pin is FATAL to the bootstrap, and the caller stops on it. Nothing
// removes the camera hook any more, so an unload would leave the engine's own
// code jumping into a detour in a module that is no longer mapped - not a race,
// a certainty. Head tracking not starting is the better of the two.
bool PinThisModule() {
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(&PinThisModule), &self)) {
        return true;
    }
    Log::Line("ERROR: this .asi could not be pinned in memory (%lu); head tracking is not "
              "starting. Nothing here can be installed safely in a module that something else "
              "could unload while a frame is being drawn.",
              GetLastError());
    return false;
}

Config g_config;
CameraHook g_camera;

// The one reader and writer of the config file. InitThread loads it; the hotkey
// thread saves through it after that.
std::optional<cameraunlock::config::ConfigOwner<Config>> g_configOwner;

// The two that own background threads are LEAKED ON PURPOSE.
//
// The module is pinned, so the only teardown that ever happens is process exit,
// and by then every other thread has already been terminated - possibly inside
// ws2_32 or holding the poller's lock. If these two had destructors left to run,
// the CRT would run them a few instructions after DllMain returns and join
// threads that no longer exist. Neither owns anything the OS does not reclaim at
// process exit, so the leak costs nothing and the join would cost a hang on the
// way out of the game.
TrackingRuntime& g_tracking = *new TrackingRuntime();
Hotkeys& g_hotkeys = *new Hotkeys();

// Which shipped build the process is, stated once at the top of the log before
// any subsystem acts on it. Each reader still reports what its own feature does
// with the answer - what an unrecognised build costs the menu suppression is not
// what it costs the field of view - but a bug report needs the fingerprint
// itself, and only this line carries it when no reader has been started yet.
void LogRunningBuild() {
    const ResolvedBuild build = ResolveRunningBuild();
    if (build.outcome == BuildLookup::Matched) {
        Log::Line("Build: %s", build.profile->name);
        return;
    }
    Log::Line("Build: %s (TimeDateStamp 0x%08X, SizeOfImage 0x%08X, CheckSum 0x%08X)",
              BuildLookupCause(build.outcome), build.fingerprint.TimeDateStamp,
              build.fingerprint.SizeOfImage, build.fingerprint.CheckSum);
}

void LogLines(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) Log::Line("%s", line.c_str());
}

// The new state is already applied when this runs; a save that fails leaves the
// session on it and says so in the log.
void Save(const std::function<void(Config&)>& change) {
    const cameraunlock::config::ConfigSaveResult saved = g_configOwner->Save(change);
    LogLines(saved.log);
    if (!saved.reason.empty()) Log::Line("WARN: %s", saved.reason.c_str());
}

void CycleModeAndSave() {
    const cameraunlock::TrackingModeChannels channels =
        cameraunlock::EncodeTrackingMode(g_tracking.CycleMode());
    Save([channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

void ToggleYawModeAndSave() {
    const bool worldSpaceYaw = g_tracking.ToggleYawMode();
    Save([worldSpaceYaw](Config& c) { c.world_space_yaw = worldSpaceYaw; });
}

DWORD WINAPI InitThread(LPVOID) {
    OpenLog();

    if (!PinThisModule()) return 0;

    // After the log and before anything that can fault, so a crash in the
    // bootstrap below is reported rather than silent. Late enough that the
    // engine's own modules are mapped, which is what makes the stack walk
    // resolve to module+RVA.
    cameraunlock::diagnostics::InstallCrashHandler();

    LogRunningBuild();

    const std::wstring iniPath = GetExePathW(kConfigFileName);
    if (iniPath.empty()) {
        Log::Line("ERROR: could not resolve the directory MetroExodus.exe is in, so %ls cannot be "
                  "read or written; head tracking is not starting",
                  kConfigFileName);
        return 0;
    }
    g_configOwner.emplace(ConfigOwnerOptionsFor(iniPath, GetExePathW(kLegacyConfigFileName),
                                                cameraunlock::config::DefaultsFile::PerUser()));
    cameraunlock::config::ConfigLoadResult<Config> loaded = g_configOwner->Load();
    LogLines(loaded.log);
    Log::Line("Config: %ls (%s)", iniPath.c_str(),
              cameraunlock::config::ConfigLoadStatusName(loaded.status));
    if (!loaded.reason.empty()) Log::Line("WARN: %s", loaded.reason.c_str());
    // The build before the canonical format did not start on a MetroExodusHeadTracking.ini
    // it refused (a port outside 1024-65535, a field of view that is neither 0 nor 60 to
    // 120), so this one does not either while CameraUnlock.ini is absent and the player
    // has not fixed it.
    if (loaded.status == cameraunlock::config::ConfigLoadStatus::LegacyRefused) {
        Log::Line("ERROR: %ls could not be loaded; head tracking is not starting", iniPath.c_str());
        return 0;
    }
    g_config = std::move(loaded.config);

    g_tracking.Start(g_config);

    // End changes the session only. The mode and yaw keys save what they switch to.
    Hotkeys::Actions actions;
    actions.toggle = [] { g_tracking.ToggleEnabled(); };
    actions.cycleMode = [] { CycleModeAndSave(); };
    actions.yawMode = [] { ToggleYawModeAndSave(); };
    g_hotkeys.Start(g_config, std::move(actions));

    // Last, because it is the one that can fail on a build the rest of the mod
    // is perfectly happy on: the tracker link, the config and the hotkeys need
    // nothing pinned, and the camera needs two addresses that a patch can move.
    // It reports its own outcome either way, so a reader who got this far knows
    // whether anything reaches the view.
    const bool cameraHooked = g_camera.Initialise(g_config, g_tracking);

    // Only when the camera hook did not land: the field of view resolves its
    // console variable from the frame loop, and on a build with no hook there is
    // no frame loop to resolve it from. BEFORE the window centring below, because
    // that resolve runs against a deadline armed back in FovState::Initialise and
    // the centring can block for the whole of it.
    if (!cameraHooked) g_camera.SettleFieldOfView();

    // Last, because it blocks until the game has a window that has stopped
    // moving - up to a minute on a cold start. Nothing here may wait on it.
    CenterWindowWhenReady();
    return 0;
}

}  // namespace

}  // namespace metroex

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            // Off the loader lock. Everything the bootstrap does - opening a
            // file, binding a socket, starting the hotkey thread - takes locks
            // the loader lock must not be held across, and the crash handler's
            // module snapshot calls into the loader itself.
            if (HANDLE thread = CreateThread(nullptr, 0, metroex::InitThread, nullptr, 0,
                                             nullptr)) {
                CloseHandle(thread);
            } else {
                // The debugger is the only channel there is: the log is opened
                // by the thread that just failed to start, so without this the
                // symptom is an .asi that was loaded and wrote no log at all -
                // which is exactly what a loader that never loaded it looks
                // like.
                OutputDebugStringA("MetroExodusHeadTracking: CreateThread failed; head tracking "
                                   "is not starting and no log will be written\n");
            }
            break;

        case DLL_PROCESS_DETACH:
            // The module is pinned, so this only ever runs at process exit -
            // every other thread has already been terminated, possibly while
            // holding the log mutex or inside ws2_32. So nothing is stopped,
            // nothing is joined and nothing is logged here, on any path. The log
            // needs no flush either way: it is written through on every line so
            // it survives a hard kill.
            //
            // What DOES run is the restore, because two of the things this mod
            // changes outlive the process: the game's field-of-view console
            // variable and its crosshair switch are settings the engine may
            // write back to disk. That is four guarded scalar stores and nothing
            // else - no lock, no allocation, no loader call.
            metroex::CameraHook::RestoreGameState();
            break;

        default:
            break;
    }
    return TRUE;
}
