#pragma once

#include "ads_gate.h"
#include "aim_projection.h"

namespace metroex {

struct CameraFrame;

// The mark the player aims with, drawn over the game's own frame.
//
// PARALLAX IS STOOD DOWN in this mod, deliberately, and the reticle projects the
// clean aim DIRECTION rather than an aim point. ProjectAimPoint takes a distance
// and is exercised against one; nothing in Metro Exodus hands the mod that
// distance. The engine's collision queries are not reachable from a static
// address, the published camera block carries no aim result, and a scan of the
// process for a world point sitting on the camera's own ray found none - which
// fits a shooter whose rounds are simulated projectiles rather than hitscans.
//
// What that leaves uncorrected is the lean term. It is the angle the eye offset
// subtends at the target, atan(lean / range), so with the head at the 0.30m
// default limit it is about 23 degrees at arm's length, 8 at two metres, 3 across
// a room and under one past twenty. It shrinks with distance and has no sign in
// it that can invert, so it is a near-range error rather than a wandering one.
// What the projection does correct is the rotation term, which is the large one
// at every range - a head turn of thirty degrees moves the aim most of the way
// across the frame.
//
// TWO CONDITIONS BRING PARALLAX BACK, and both have to hold:
//   1. a per-frame distance along the CLEAN aim to the surface the rounds stop
//      on, measured on the frame that consumes it, filtered by collision layer
//      and mask rather than by object name, and
//   2. an opposite-lean shot test showing both rounds landing in one hole, so
//      the aim is known to be decoupled before any lean term is added.
// With a distance in hand the call site swaps ProjectAimDirection for
// ProjectAimPoint and nothing else changes.
class Reticle {
public:
    // Resolves the game's own crosshair switch and readies the marker. The
    // overlay itself installs lazily, on the first frame that asks for it.
    void Initialise();

    // Called once per rendered frame from the camera hook, with the frame that
    // hook just built and the ADS mode that frame was decided under. Publishes
    // the mark's position and takes the game's own crosshair off the screen
    // while the mod owns the aim.
    //
    // `mode` is what makes the mark part of the ADS cycle rather than something
    // that runs underneath it: with the sights up only `marker` draws, and the
    // other two hand the crosshair back to the game. AimMarkerApplies() in
    // ads_gate.h is the rule, and this call site is its only caller.
    void Update(const CameraFrame& frame, const HalfFieldTangents& tangents, AdsMode mode);

    // Puts the game's crosshair back. Runs at process exit only (the module is
    // pinned - see dllmain.cpp), so it is one guarded store and nothing that can
    // block or log.
    void Restore();
};

}  // namespace metroex
