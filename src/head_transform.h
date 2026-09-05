#pragma once

#include "cameraunlock/math/vec3.h"

namespace metroex {

using cameraunlock::math::Vec3;

// The camera as 4A publishes it: a position and an orthonormal basis, in a
// LEFT-handed world with +Y up. Measured, not assumed - in a running game the
// published triple satisfies right == cross(up, forward), which is the
// left-handed identity; the right-handed one gives the opposite vector.
struct CameraBasis {
    Vec3 position;
    Vec3 forward;
    Vec3 up;
    Vec3 right;
};

// One frame of processed head tracking, in the core convention: Euler degrees
// for rotation, metres for position with NEGATIVE z as the forward lean.
struct HeadPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool has_position = false;
};

// A head pose already converted to this engine's conventions, so nothing below
// has to know what the tracker's signs meant.
//
//   yaw   > 0 turns the view to the player's right
//   pitch > 0 raises the view
//   roll  > 0 rotates the view about the forward axis by the right-hand rule
//   x     > 0 moves the eye along the camera's right axis, metres
//   y     > 0 moves it up, z > 0 moves it forward
struct EngineHeadPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// The one place a tracker sign becomes an engine sign. Yaw, pitch, roll and the
// vertical lean pass through; the lateral and forward leans are negated.
//
// EVERY ONE OF THOSE WAS SETTLED BY A PLAYER, and the order they were settled in
// is the useful part. The mod shipped negating yaw, roll and x - the fleet
// default AGENTS.md says to start from. In game the view turned the wrong way and
// rolled the wrong way, so yaw and roll came out. x came out with them, on the
// argument that reflecting a head about its own sagittal plane negates exactly
// yaw, roll and x, so a report about two of them is a report about all three.
//
// THAT ARGUMENT WAS WRONG HERE. The player then reported the lean going the wrong
// way, and x went back to negated. So the three do not move as a block on this
// engine, and the next person to touch these signs should change ONE axis per
// report and wait to be told, rather than reasoning about which others must
// follow.
//
// z is negated for a different reason and is not a tracker sign at all: the
// core's convention puts the forward lean on NEGATIVE z while this engine's
// camera-local forward is +z.
//
// tests/projection_tests.cpp pins all six axes so a flipped sign fails there
// rather than in front of a player.
EngineHeadPose ToEngineConvention(const HeadPose& pose);

// Rotates `v` about `axis` by `degrees` using the right-hand rule. `axis` must
// be unit length.
Vec3 RotateAboutAxis(const Vec3& v, const Vec3& axis, float degrees);

// The camera the frame is drawn from.
//
// Rotation composes yaw, then pitch, then roll, each about the axis the
// previous step left behind:
//
//   yaw   about the world up-axis (horizon locked) or the camera's own,
//   pitch about the right axis the yaw produced,
//   roll  about the forward axis the pitch produced.
//
// `worldSpaceYaw` picks the yaw axis. On the world up-axis the horizon stays
// level however far the mouse has pitched the camera; on the camera's own it
// leans once the camera is pitched steeply.
//
// The eye moves in the CLEAN basis, not the rotated one, so a lean follows the
// body rather than the head: leaning left while looking over your shoulder
// moves you left, not backwards.
CameraBasis ApplyHeadPose(const CameraBasis& clean, const EngineHeadPose& pose, bool worldSpaceYaw);

}  // namespace metroex
