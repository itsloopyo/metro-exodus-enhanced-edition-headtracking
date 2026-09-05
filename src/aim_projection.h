#pragma once

#include "fov.h"
#include "head_transform.h"

namespace metroex {

// Where the shot lands, in the frame that is actually being drawn.
struct AimScreenPoint {
    // Normalised device coordinates of the drawn frame: -1..1 left to right,
    // -1..1 bottom to top.
    float ndc_x = 0.0f;
    float ndc_y = 0.0f;

    // False when the point cannot be drawn: behind the eye, on the plane of the
    // eye, or with a field of view the perspective divide cannot be done with.
    // A consumer hides the reticle rather than drawing at 0,0, which is the one
    // place a wrong reticle looks right.
    bool valid = false;
};

// The reticle marks a POINT, not a direction.
//
// With the eye where the game put it the two project to the same place, which
// is why the direction form below is what the mod calls today. A lean breaks it:
// the frame is drawn from an eye up to 30cm to one side of the one the shot
// leaves from, so the fixed impact point is no longer straight ahead of the
// drawn view. A reticle built on a direction slides off the thing it marks,
// worse the closer the target.
//
// `shotEye` and `aimDirection` describe the CLEAN camera - where the game
// thinks it is aiming, and where the rounds therefore go. `drawn` is the basis
// the camera hook wrote for this frame. `distance` is how far along the clean
// aim the rounds stop, measured this frame; there is deliberately no default,
// no smoothing and no weapon-convergence constant, because an error
// proportional to lean * (1/d0 - 1/d) vanishes at exactly one range and changes
// side either side of it.
//
// Nothing in Metro Exodus hands the mod a distance yet, so `Reticle` calls the
// direction form below and this one is reached only from
// tests/projection_tests.cpp, where it is what pins the projection against the
// engine's own view and projection matrices. reticle.h records the two
// conditions that put it back on the call path.
AimScreenPoint ProjectAimPoint(const CameraBasis& drawn, const Vec3& shotEye,
                               const Vec3& aimDirection, float distance,
                               const HalfFieldTangents& tangents);

// The same projection for a definite no-hit: a target at infinity, where the
// eye offset cancels and only the direction matters. Never a stand-in for a
// distance that could not be measured - that case invalidates instead.
AimScreenPoint ProjectAimDirection(const CameraBasis& drawn, const Vec3& aimDirection,
                                   const HalfFieldTangents& tangents);

}  // namespace metroex
