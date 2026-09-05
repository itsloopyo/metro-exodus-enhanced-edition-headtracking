#include "head_transform.h"

#include <cmath>

namespace metroex {

namespace {

constexpr float kDegToRad = 0.01745329252f;

// Yaw on the world up-axis needs an axis the camera basis does not contain, so
// the whole composition goes through Rodrigues rather than the cheaper
// two-vector plane rotations. One code path for both yaw modes is worth more
// than the handful of multiplies.
Vec3 Ortho(const Vec3& v, const Vec3& against) {
    return (v - against * Vec3::Dot(v, against)).Normalized();
}

}  // namespace

EngineHeadPose ToEngineConvention(const HeadPose& pose) {
    EngineHeadPose e;
    e.yaw = pose.yaw;
    e.pitch = pose.pitch;
    e.roll = pose.roll;
    e.x = -pose.x;
    e.y = pose.y;
    e.z = -pose.z;
    return e;
}

Vec3 RotateAboutAxis(const Vec3& v, const Vec3& axis, float degrees) {
    const float rad = degrees * kDegToRad;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    const Vec3 cross = Vec3::Cross(axis, v);
    const float dot = Vec3::Dot(axis, v);
    return v * c + cross * s + axis * (dot * (1.0f - c));
}

CameraBasis ApplyHeadPose(const CameraBasis& clean, const EngineHeadPose& pose,
                          bool worldSpaceYaw) {
    CameraBasis out = clean;

    // Positive yaw about the up-axis turns the forward vector toward the right
    // vector: with right == cross(up, forward), Rodrigues about up sends
    // forward through cross(up, forward), which is right by definition. So
    // "positive yaw looks right" is a consequence of the measured handedness
    // rather than a convention chosen here.
    if (pose.yaw != 0.0f) {
        const Vec3 axis = worldSpaceYaw ? Vec3(0.0f, 1.0f, 0.0f) : clean.up;
        out.forward = RotateAboutAxis(out.forward, axis, pose.yaw);
        out.up = RotateAboutAxis(out.up, axis, pose.yaw);
        out.right = RotateAboutAxis(out.right, axis, pose.yaw);
    }

    // Negated, because Rodrigues about the right axis sends forward through
    // cross(right, forward), which is -up. Pitching up is what a positive pitch
    // has to mean everywhere else in the mod, so the sign is spent here rather
    // than at the tracker boundary where it would read as a tracker fix.
    if (pose.pitch != 0.0f) {
        out.forward = RotateAboutAxis(out.forward, out.right, -pose.pitch);
        out.up = RotateAboutAxis(out.up, out.right, -pose.pitch);
    }

    // Roll leaves the forward axis where it is and nothing below reads the right
    // axis before it is re-derived, so rolling `right` here would be a Rodrigues
    // rotation whose result the cross product overwrites.
    if (pose.roll != 0.0f) {
        out.up = RotateAboutAxis(out.up, out.forward, pose.roll);
    }

    // Three successive Rodrigues rotations of three separately stored vectors
    // drift out of orthogonality in float. The engine builds the view matrix
    // from these directly and does not renormalise, so a skewed basis becomes a
    // skewed picture; re-deriving right from up and forward costs one cross
    // product and removes the whole class.
    out.forward = out.forward.Normalized();
    out.up = Ortho(out.up, out.forward);
    out.right = Vec3::Cross(out.up, out.forward);

    // In the clean basis, not the rotated one: the lean follows the body.
    out.position = clean.position + clean.right * pose.x + clean.up * pose.y +
                   clean.forward * pose.z;
    return out;
}

}  // namespace metroex
