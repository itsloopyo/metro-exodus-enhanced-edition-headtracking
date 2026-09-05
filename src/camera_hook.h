#pragma once

#include "ads_gate.h"
#include "head_transform.h"

namespace metroex {

struct Config;
class TrackingRuntime;

// What the camera hook did on the last frame it ran, for anything that has to
// draw in that frame's coordinates.
struct CameraFrame {
    // Where the game thinks the camera is. The aim, the raycasts and the
    // projectiles all come off this, whether head tracking is running or not.
    CameraBasis clean;

    // The camera the frame was actually drawn from. Equal to `clean` when
    // tracking is not being applied.
    CameraBasis drawn;

    // The verdict the ADS gate reached for this frame, so a consumer can tell
    // "tracking is off" from "there is no tracker" without asking again.
    TrackingState state;
};

// Head tracking's one write into the engine.
//
// 4A publishes its camera into a single static block and then derives the view
// matrix, the view-projection and the frustum planes from it. The hook sits on
// that derivation: it rewrites the position and basis in the block, lets the
// engine build everything from the rewritten camera, and puts the clean values
// back before returning.
//
// Both halves of that matter. Building the derived matrices from the tracked
// camera is what makes the head turn cull correctly - the frustum comes out of
// the same view-projection the frame is drawn with, so scenery that comes into
// view comes into the frustum with it. Putting the clean values back is what
// keeps the game's own reads - aim, weapons, raycasts, AI - looking at the
// camera the player is actually aiming with.
class CameraHook {
public:
    // Matches the running EXE against the build profiles, resolves the camera
    // block and the builder, and installs the hook. Logs the outcome either
    // way; a build with no matching profile leaves the camera untouched.
    bool Initialise(const Config& cfg, TrackingRuntime& tracking);

    // Puts back the only two things this mod changes that can outlive the
    // process: the `r_base_fov_option` console variable (its value and the two
    // bounds the override widened) and the `g_show_crosshair` byte. Both are
    // settings the game may write to its own config, so a player who removes the
    // mod must not be left holding them.
    //
    // Nothing else is undone, and nothing here can block or log. The six bytes
    // of the field-of-view pin are not put back because a .text patch dies with
    // the process that carries it, and THE HOOK IS NOT REMOVED: MinHook returns
    // the trampoline to its pool the moment a hook is removed, while the render
    // thread can be anywhere inside the detour, so an unhook races a call
    // through recycled memory. The module is pinned instead, in InitThread (see
    // dllmain.cpp, which says why not in DllMain), so this is reachable only at
    // DLL_PROCESS_DETACH during process exit - every other thread already
    // stopped, possibly holding the log mutex, hence no logging.
    static void RestoreGameState();

    // Drives the field-of-view console variable's resolve from the calling
    // thread until it answers. Only for a build Initialise() returned false on:
    // that resolve normally rides the frame loop, and with no hook there is no
    // frame loop, while the field-of-view override is the one thing on such a
    // build that still works. Sleeps between attempts for up to a minute in the
    // worst case, so nothing the game waits on may call it.
    static void SettleFieldOfView();
};

}  // namespace metroex
