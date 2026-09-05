// The mod's reticle, drawn over the game's Direct3D 12 frame.
//
// The projection lives in aim_projection.cpp and the drawing lives in
// cameraunlock-core; this file is the join between them, plus the one write
// that takes the game's own crosshair off the screen while the mod owns the
// aim. Two marks would be worse than either alone: the stock crosshair sits at
// the middle of the frame, which stops being where the rounds go the moment the
// head turns, and leaving it there beside a correct one invites the player to
// use the wrong one.
//
// The cost of that write is worth stating plainly: Metro's crosshair is a
// dispersion cross whose rays spread with the weapon's accuracy, and the mark
// drawn here does not carry that. It is the same trade the fleet's other C++
// mods make, and it only applies while head tracking is actually reaching the
// view - the moment tracking is switched off, suppressed by the game state, or
// left without a tracker, the game's own crosshair goes back.

#define CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_DX12_IMPLEMENTATION

#include "cameraunlock/rendering/aim_marker_dx12.h"

#include "build_profile.h"
#include "camera_hook.h"
#include "console_var.h"
#include "logging.h"
#include "reticle.h"

#include "cameraunlock/memory/safe_memory.h"

#include <windows.h>

namespace metroex {

namespace {

constexpr char kCrosshairCvarName[] = "g_show_crosshair";

cameraunlock::rendering::AimMarkerDX12 g_marker;

// The `g_show_crosshair` console variable's value byte, or null when the build
// profile does not carry the variable object or the object did not check out.
uint8_t* g_gameCrosshair = nullptr;

// What to put back when the mod hands the crosshair over. Re-read from the game
// on the frame the mark takes it away rather than captured once at resolve time:
// a player who switches their own crosshair off mid-session would otherwise have
// it switched back on by the next restore and never be able to keep it off.
uint8_t g_gameCrosshairOn = 0;
bool g_gameCrosshairHidden = false;

// The last value the mod itself wrote. A byte that differs from it was written
// by the game, so it is the player's choice and worth latching; a byte equal to
// it is the mod's own and says nothing.
uint8_t g_lastWritten = 0;
bool g_haveWritten = false;

bool g_loggedFirstDraw = false;
bool g_loggedNoOverlay = false;

// Retried from the frame loop rather than resolved once at startup - see
// console_var.h for the half-built object that makes a single attempt a coin
// flip. The first build of this did resolve once, and reported a perfectly good
// address as wrong.
enum class Resolve { Pending, Resolved, Failed };
Resolve g_resolve = Resolve::Failed;
uint32_t g_cvarRva = 0;
const uint8_t* g_imageBase = nullptr;
uint32_t g_imageSize = 0;
uint64_t g_resolveDeadlineMs = 0;

// Why the last attempt failed, so the line written when the retries run out says
// which of the object's questions it failed rather than only that it failed.
// Points at a string literal. Without it every outcome was reported as "the
// variable never appeared where the build profile says it is", which sends a bug
// report at the profile's RVA even when the address was right.
const char* g_resolveFailure = nullptr;

// The overlay reports its own install failures - a refused Present hook, a swap
// chain it cannot use, a shader that would not compile - and without this they
// go nowhere. All the mod can say on its own is that the overlay never came up,
// which is the symptom rather than the cause.
void ForwardOverlayLog(const char* message) { Log::Line("%s", message); }

void DropCrosshairSlot() {
    // The slot has gone. Stop writing rather than fault on it every frame, and
    // leave the player with the game's own crosshair beside the mod's - visibly
    // wrong, which is what gets it reported.
    Log::Line("ERROR: the game's crosshair switch could not be written and has been "
              "dropped; the game's own crosshair may be drawn beside the mod's");
    g_gameCrosshair = nullptr;
}

void SetGameCrosshair(bool visible) {
    if (g_gameCrosshair == nullptr) return;
    const uintptr_t slot = reinterpret_cast<uintptr_t>(g_gameCrosshair);

    if (!visible) {
        uint8_t current = 0;
        if (!cameraunlock::memory::SafeReadU8(slot, current)) {
            DropCrosshairSlot();
            return;
        }
        // Anything the mod did not write is the player's setting.
        if (!g_haveWritten || current != g_lastWritten) g_gameCrosshairOn = current;
        if (current == 0) {
            // Already off, by the player's choice or by the mod's last write.
            // Nothing to hide, and nothing to put back - but the zero still has
            // to be recorded as the mod's last known value, or a player who
            // later switches their crosshair back ON writes the same 1 the mod
            // would have written and the change goes unnoticed.
            g_lastWritten = 0;
            g_haveWritten = true;
            g_gameCrosshairHidden = true;
            return;
        }
    } else if (!g_gameCrosshairHidden) {
        return;
    }

    const uint8_t value = visible ? g_gameCrosshairOn : uint8_t{0};
    if (!cameraunlock::memory::SafeWrite(slot, value)) {
        DropCrosshairSlot();
        return;
    }
    g_lastWritten = value;
    g_haveWritten = true;
    g_gameCrosshairHidden = !visible;
}

// One attempt at the console-variable object: does it name the variable it is
// supposed to, and is the value slot it hands back a byte of the game's own
// image that is currently switched on? Every one of those is checked before
// anything is written, because a matched name can sit beside a value pointer the
// constructor has not written yet.
bool TryResolveCrosshairCvar() {
    const uintptr_t cvar = reinterpret_cast<uintptr_t>(g_imageBase) + g_cvarRva;
    switch (CheckCvarName(cvar, kCrosshairCvarName)) {
        case CvarName::Matches:
            break;
        case CvarName::Unreadable:
            g_resolveFailure = "the name at that address could not be read";
            return false;
        case CvarName::Different:
            g_resolveFailure = "the object there does not name that variable";
            return false;
    }

    uintptr_t valuePtr = 0;
    if (!cameraunlock::memory::SafeRead(cvar + kCvarValueOffset, valuePtr)) {
        g_resolveFailure = "the variable has no value slot";
        return false;
    }
    if (!AddressFitsImage(reinterpret_cast<const void*>(valuePtr), g_imageBase, sizeof(uint8_t),
                          g_imageSize)) {
        g_resolveFailure = "its value slot is outside the game's own image";
        return false;
    }

    uint8_t current = 0;
    if (!cameraunlock::memory::SafeReadU8(valuePtr, current)) {
        g_resolveFailure = "its value slot could not be read";
        return false;
    }

    // A crosshair already switched off resolves fine - the address is good, and
    // the resolve is what the mod needs in order to hand the switch back later.
    // What it does NOT do is make the mod draw: see Update(), which leaves a
    // player who turned their own crosshair off with no mark at all rather than
    // putting one on screen they did not ask for.
    g_gameCrosshair = reinterpret_cast<uint8_t*>(valuePtr);
    g_gameCrosshairOn = current;
    return true;
}

// Called from the frame loop until it answers, so the object it reads is the
// finished one. Reports the verdict once, either way.
bool CrosshairSwitchReady() {
    switch (g_resolve) {
        case Resolve::Resolved:
            return true;
        case Resolve::Failed:
            return false;
        case Resolve::Pending:
            break;
    }
    if (TryResolveCrosshairCvar()) {
        g_resolve = Resolve::Resolved;
        Log::Line("Reticle: the mark follows the aim through the head-turned view; the game's "
                  "own crosshair is hidden while it does. Head lean is not compensated - see "
                  "the note in reticle.h.");
        return true;
    }
    if (GetTickCount64() >= g_resolveDeadlineMs) {
        g_resolve = Resolve::Failed;
        Log::Line("ERROR: the %s console variable did not resolve (%s); the mod's mark is not "
                  "drawn",
                  kCrosshairCvarName, g_resolveFailure);
    }
    return false;
}

}  // namespace

void Reticle::Initialise() {
    // Before anything can call Ensure(), which is what the overlay's own note on
    // this setter asks for: it writes shared state without a lock, and every
    // Ensure() comes off a render frame.
    g_marker.SetLogger(&ForwardOverlayLog);

    const ResolvedBuild build = ResolveRunningBuild();
    if (build.outcome != BuildLookup::Matched || build.profile->crosshair_cvar_rva == 0) {
        Log::Line("Reticle: the game's crosshair switch has not been derived on this build; "
                  "the mod's mark is not drawn, because it would be drawn beside the game's "
                  "own");
        return;
    }
    const BuildProfile& p = *build.profile;
    if (!RvaFits(p.crosshair_cvar_rva, kCvarValueSlotBytes, build.fingerprint.SizeOfImage)) {
        Log::Line("ERROR: build profile %s puts the crosshair variable outside the image; the "
                  "mod's mark is not drawn",
                  p.name);
        return;
    }
    g_cvarRva = p.crosshair_cvar_rva;
    g_imageBase = build.base;
    g_imageSize = build.fingerprint.SizeOfImage;
    g_resolve = Resolve::Pending;
    g_resolveDeadlineMs = GetTickCount64() + kCvarResolveWindowMs;
}

void Reticle::Update(const CameraFrame& frame, const HalfFieldTangents& tangents, AdsMode mode) {
    // Without the crosshair switch there is no way to take the game's own mark
    // off the screen, and two marks are worse than the stock one alone.
    if (!CrosshairSwitchReady()) return;

    // Recomputed here every frame rather than carried over, so the sights coming
    // up, the sights going down and a mid-aim press of the cycle key all land on
    // the frame they happen on. With the sights up this is what separates
    // `marker` from the two modes that draw nothing.
    // The player's own crosshair setting decides whether the mod draws at all.
    // With it off there is no mark to replace, and putting one on screen is not
    // this mod's decision to make. Taken from SetGameCrosshair's latch rather
    // than from the resolve, so switching it off mid-session takes effect.
    if (!AimMarkerApplies(frame.state.verdict, frame.state.aiming, mode) ||
        g_gameCrosshairOn == 0) {
        g_marker.Publish(false, 0.0f, 0.0f);
        SetGameCrosshair(true);
        return;
    }

    if (!tangents.valid) {
        // No field of view means no perspective divide, so there is no mark to
        // draw. Hand the crosshair back rather than leaving the player with
        // neither: a projection the mod cannot do is not a reason to take away
        // the one the game was already drawing.
        g_marker.Publish(false, 0.0f, 0.0f);
        SetGameCrosshair(true);
        return;
    }

    if (!g_marker.Ensure()) {
        if (!g_loggedNoOverlay) {
            g_loggedNoOverlay = true;
            Log::Line("Reticle: the overlay is not up yet; the game's own crosshair stands "
                      "until it is");
        }
        SetGameCrosshair(true);
        return;
    }

    // The clean forward is where the game is aiming and where the rounds go.
    // Projected through the basis the frame was drawn with, that is the point on
    // screen the player has to put on a target.
    const AimScreenPoint aim = ProjectAimDirection(frame.drawn, frame.clean.forward, tangents);
    if (!aim.valid) {
        // Turned so far that the aim is on or behind the plane of the eye.
        // Publishing false rather than clamping to the edge: a mark on the edge
        // says "the rounds go there", and they do not. The crosshair stays
        // hidden, because the mod still owns the aim - the game's own mark at
        // the middle of the frame would be wronger still.
        g_marker.Publish(false, 0.0f, 0.0f);
        SetGameCrosshair(false);
        return;
    }

    g_marker.Publish(true, aim.ndc_x, aim.ndc_y);
    SetGameCrosshair(false);
    if (!g_loggedFirstDraw) {
        g_loggedFirstDraw = true;
        Log::Line("Reticle: drawing");
    }
}

void Reticle::Restore() {
    if (g_gameCrosshair == nullptr || !g_gameCrosshairHidden) return;

    // Guarded, and silent, and deliberately not SetGameCrosshair(): this runs
    // from DLL_PROCESS_DETACH, where every other thread has been terminated
    // where it stood and may hold the log mutex or the CRT heap lock, so the
    // ERROR line that path writes on a failed store would hang the game on the
    // way out instead of closing it. Nor is the marker published: there is no
    // frame left to draw, and the first Publish on a build that never drew would
    // construct a function-local static under the loader's own once-lock.
    cameraunlock::memory::SafeWrite(reinterpret_cast<uintptr_t>(g_gameCrosshair),
                                    g_gameCrosshairOn);
    g_gameCrosshairHidden = false;
}

}  // namespace metroex
