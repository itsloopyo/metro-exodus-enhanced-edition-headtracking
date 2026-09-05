#pragma once

#include <cmath>
#include <cstdint>

namespace metroex {

struct Config;

// The half-field tangents of the frame the player is looking at: tan(fovX/2)
// and tan(fovY/2). This is the form every projection in the mod wants, because
// a perspective divide ends in a division by exactly these two numbers, and it
// is the form the engine itself carries - 4A stores a vertical FOV in degrees
// and a separate horizontal multiplier, and its own screen-to-world
// unprojection builds the ray as
//
//   dir = fwd*n + up*(ndcY * n*tan(fovV/2)) + right*(ndcX * n*tan(fovV/2)*aspect)
//
// so the horizontal half-field is the vertical one times that multiplier rather
// than anything derived from the window size.
struct HalfFieldTangents {
    float x = 0.0f;
    float y = 0.0f;

    // False before the game has drawn its first frame, when the camera globals
    // are still zero, and for any FOV the perspective divide cannot be done
    // with. A consumer must hide its reticle rather than draw at whatever
    // x and y happen to hold.
    bool valid = false;
};

// The vertical FOV is in degrees and `aspect` is the engine's own horizontal
// multiplier, not a width/height computed by the mod. They are separate because
// the engine keeps them separate: `aspect` is the viewport ratio times a
// pixel-aspect term the engine owns, and a mod that recomputed it from the
// backbuffer would agree at 16:9 and drift on anything else.
inline HalfFieldTangents TangentsFromCameraFov(float verticalFovDegrees, float aspect) {
    HalfFieldTangents t;
    // Written as `!(x > lo)` rather than `x <= lo` so a NaN in either argument
    // fails the test instead of passing it and reaching std::tan.
    if (!(verticalFovDegrees > 0.0f) || !(verticalFovDegrees < 180.0f) || !(aspect > 0.0f)) {
        return t;
    }
    constexpr float kDegToRad = 0.01745329252f;
    t.y = std::tan(verticalFovDegrees * 0.5f * kDegToRad);
    t.x = t.y * aspect;
    t.valid = t.y > 0.0f && t.x > 0.0f && std::isfinite(t.x);
    return t;
}

// Where the mod reads the field of view from, and the one place it writes it.
//
// READING. The camera FOV is not a setting; it is a per-frame product. The
// engine computes it as
//
//   camera FOV = per-camera coefficient * base FOV, clamped to [0.01, 179]
//
// and publishes the result to a global the rest of the engine reads. The
// coefficient is where aiming down sights, scopes, binoculars, vehicles and
// cutscenes all live, so the published value is the only number that describes
// the frame actually being drawn. Nothing else does: the base FOV alone is
// wrong the moment the player raises a weapon, and the INI value below is wrong
// as well as stale. So the projection reads the global, per frame, every frame.
//
// WRITING. The game's own Field of View slider is the base FOV, and the engine
// refuses to set it outside 60 to 75 degrees - the console variable carries its
// own bounds and its setter rejects anything past them with "invalid syntax".
// The override widens those bounds and writes the value, which is why the
// setting reaches the whole engine (culling, HUD scale, the sight picture)
// rather than only the picture, and why it needs no separate handling in any
// projection: it moves the base FOV, the engine recomputes the camera FOV from
// it, and the reads above pick the new value up on the next frame.
//
// One more thing stands between the console variable and the picture, and the
// override has to take it out of the way. While a level is loaded the engine
// does not ease its live base FOV toward the console variable at all: the target
// is chosen on the level-state byte, and in a level it is a constant 60 degrees
// in .rdata. That is measured, not inferred - with the variable at 90 the live
// base sat at exactly 60.000 for a whole level. So writing the console variable
// alone moves nothing in gameplay. The override therefore also replaces the one
// conditional jump that makes that choice with NOPs, which leaves the engine
// easing toward its own console variable in a level. Nothing is patched when the
// override is off.
//
// THE MAIN MENU FOLLOWS NEITHER. Measured with the pin lifted: state byte 0,
// console variable 90, live base 60.000, because the frame delta the easing
// multiplies by is 0 there. So the override is judged in a level, and every log
// line here says so rather than offering the menu as a consolation.
//
// The console variable is resolved LAZILY, retried from whatever drives Update()
// until it answers. The engine builds its console-variable table while its
// statics are still being constructed, and an .asi is loaded before that
// finishes, so a resolve at load time reads a half-built object about half the
// time: the constructor stores the name at +0x08 before the value pointer at
// +0x18, so the name matches while the pointer is still whatever the memory
// held. Resolving once at load is what made this report a perfectly good
// address as wrong on one launch and take it on the next.
class FovState {
public:
    // Matches the running EXE against the build profiles and pins the two
    // camera globals. On a build with no matching profile nothing is resolved:
    // the projection falls back to nothing (Tangents() stays invalid, so a
    // consumer hides its reticle rather than drawing it in the wrong place) and
    // the override is reported as unavailable rather than written blind.
    void Initialise(const Config& cfg);

    // Call once per rendered frame, before anything reads Tangents(). Drives the
    // console-variable resolve above as well as re-asserting the override.
    void Update();

    // The frame's half-field tangents, or an invalid pair when the camera
    // globals have not been resolved or have not been written yet.
    HalfFieldTangents Tangents() const;

    // Puts the console variable back: the value the override wrote, and the two
    // bounds it widened to get the value past the game's own setter. Safe to call
    // having changed neither.
    //
    // The six patched bytes are NOT put back, and there is nothing to put them
    // back for: a .text patch lives in the process's own memory and dies with it,
    // and this only ever runs at process exit (the module is pinned - see
    // dllmain.cpp). The console variable is different because the game may write
    // it to its own settings file, so a value outside the range its setter
    // accepts would outlive the mod on a machine it has been removed from.
    //
    // Runs at DLL_PROCESS_DETACH, where every other thread has already been
    // terminated in place and may be holding the log mutex or the CRT heap lock.
    // So: three guarded scalar stores, no logging, no VirtualProtect, nothing
    // that can block.
    void Restore();

    // False while the console variable is still being retried. A caller driving
    // Update() from outside the frame loop - which is the only driver on a build
    // the camera hook could not land on - uses this to know when to stop.
    //
    bool OverrideSettled() const { return m_resolve != Resolve::Pending; }

private:
    enum class Resolve { Pending, Resolved, Failed };

    // One attempt at the console-variable object: does it name the variable it
    // is supposed to, and is the value slot it hands back a static float of the
    // game's own image? Answers false for a half-built object as readily as for
    // a wrong address, which is why the caller retries rather than reporting.
    bool TryResolveBaseFovCvar();

    // Widens the console variable's own bounds far enough to admit the override,
    // takes the level pin out of the way, and reports what the field of view is
    // now doing. Only reached once the value slot and bounds are resolved.
    void ApplyOverride();

    // Replaces the level pin with NOPs, having first checked that the bytes
    // there are the jump the profile says they are. False, having logged why,
    // when they are not: a wrong six bytes written into the game's own code is a
    // crash a few frames later, so this refuses rather than guesses.
    bool RemoveLevelPin();

    // Written by the engine once per camera update on the render thread, read
    // here on the same thread.
    const volatile float* m_cameraFov = nullptr;
    const volatile float* m_cameraAspect = nullptr;

    // The console variable's own value slot, taken from the variable object
    // rather than pinned separately, so one address routes both the bounds and
    // the value they guard.
    float* m_baseFovSetting = nullptr;
    float* m_baseFovMin = nullptr;
    float* m_baseFovMax = nullptr;

    // 0 when the INI leaves the game's own setting alone.
    float m_overrideFov = 0.0f;

    // What the retry above is retrying against. `m_cvar` is 0 until Initialise
    // pins it, which is what leaves the resolve Failed on a build that carries
    // no address for it.
    Resolve m_resolve = Resolve::Failed;
    uintptr_t m_cvar = 0;
    const uint8_t* m_imageBase = nullptr;
    uint32_t m_imageSize = 0;
    uint64_t m_resolveDeadlineMs = 0;
    const char* m_profileName = nullptr;

    // The level pin. Null when the build carries no address for it. The bytes
    // that were there are not kept: nothing puts them back, because the patch
    // cannot outlive the process that carries it.
    uint8_t* m_pin = nullptr;

    // Why the last attempt failed, so the line written when the retries run out
    // says which of the two questions the object failed rather than only that it
    // failed. Points at a string literal.
    const char* m_resolveFailure = nullptr;

    // What the console variable held before the override widened its bounds and
    // wrote its value, so Restore() can put all three back. `m_savedSlot` is
    // what says they are worth putting back. Without it the engine shuts down
    // holding a field of view outside the range its own setter accepts, and a
    // player who removes the mod keeps whatever the game serialised.
    //
    // Held separately from m_baseFovSetting, which the per-frame write nulls if
    // it ever faults: the values still need putting back after that, and it is
    // the frame that dropped the pointer that made putting them back matter.
    float* m_savedSlot = nullptr;
    float m_savedFov = 0.0f;
    float m_savedMin = 0.0f;
    float m_savedMax = 0.0f;

};

}  // namespace metroex
