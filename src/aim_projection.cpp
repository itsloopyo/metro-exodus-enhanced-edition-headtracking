#include "aim_projection.h"

#include <cmath>

namespace metroex {

namespace {

// How much of the aim vector has to lie in front of the eye before the
// perspective divide is trusted. Rejecting only a negative forward component
// is not enough: as it approaches zero the projection runs off to infinity, and
// a reticle at 1e30 is a NaN on its way to a vertex buffer. One part in a
// thousand of the vector's own length keeps the divide bounded without moving
// the cutoff with the scene scale.
constexpr float kMinForwardFraction = 0.001f;

AimScreenPoint Project(const CameraBasis& drawn, const Vec3& aimVec,
                       const HalfFieldTangents& tangents) {
    AimScreenPoint out;
    if (!tangents.valid) return out;

    const float len = aimVec.Magnitude();
    if (!(len > 0.0f)) return out;

    const float f = Vec3::Dot(aimVec, drawn.forward);
    if (!(f > len * kMinForwardFraction)) return out;

    const float x = Vec3::Dot(aimVec, drawn.right) / f / tangents.x;
    const float y = Vec3::Dot(aimVec, drawn.up) / f / tangents.y;
    if (!std::isfinite(x) || !std::isfinite(y)) return out;

    out.ndc_x = x;
    out.ndc_y = y;
    out.valid = true;
    return out;
}

}  // namespace

AimScreenPoint ProjectAimPoint(const CameraBasis& drawn, const Vec3& shotEye,
                               const Vec3& aimDirection, float distance,
                               const HalfFieldTangents& tangents) {
    if (!(distance > 0.0f) || !std::isfinite(distance)) return AimScreenPoint{};
    const Vec3 impact = shotEye + aimDirection * distance;
    return Project(drawn, impact - drawn.position, tangents);
}

AimScreenPoint ProjectAimDirection(const CameraBasis& drawn, const Vec3& aimDirection,
                                   const HalfFieldTangents& tangents) {
    return Project(drawn, aimDirection, tangents);
}

}  // namespace metroex
