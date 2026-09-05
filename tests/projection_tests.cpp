// The camera composition the hook writes, and the reticle projection that has
// to agree with it.
//
// Two things are being pinned here. The first is the composition itself: which
// way a positive yaw turns, which axis a pitch turns about, and that the basis
// stays orthonormal after three rotations of three separately stored vectors -
// the engine builds its view matrix straight out of these and never
// renormalises, so a skewed basis is a skewed picture.
//
// The second is that the reticle lands where the shot lands. The check that
// makes that mean something is BuildsTheSameScreenPositionAsTheEnginesOwnMatrices:
// it builds the view and projection matrices the way 4A builds them - columns
// of the view matrix are the basis, the projection carries 1/tan of the half
// field - projects a world point through them by hand, and requires the mod's
// basis-to-basis projection to agree. Without it every other check here is the
// projection agreeing with itself.

#include <cmath>
#include <limits>

#include "aim_projection.h"
#include "head_transform.h"
#include "test_harness.h"

namespace {

using metroex::AimScreenPoint;
using metroex::CameraBasis;
using metroex::EngineHeadPose;
using metroex::HalfFieldTangents;
using metroex::HeadPose;
using metroex::Vec3;
using metroex_test::Check;

constexpr float kTol = 1e-4f;

void CheckNear(float actual, float expected, const char* what) {
    metroex_test::CheckNear(actual, expected, kTol, what);
}

// A camera looking along +x with +y up, which is the shape the running game
// publishes: forward, up, and right == cross(up, forward).
CameraBasis LevelCamera(Vec3 position = Vec3(0.0f, 0.0f, 0.0f)) {
    CameraBasis b;
    b.position = position;
    b.forward = Vec3(1.0f, 0.0f, 0.0f);
    b.up = Vec3(0.0f, 1.0f, 0.0f);
    b.right = Vec3::Cross(b.up, b.forward);
    return b;
}

CameraBasis PitchedDownCamera() {
    CameraBasis b;
    b.position = Vec3(0.0f, 0.0f, 0.0f);
    b.forward = Vec3(0.0f, -1.0f, 0.0f);
    b.up = Vec3(1.0f, 0.0f, 0.0f);
    b.right = Vec3::Cross(b.up, b.forward);
    return b;
}

HalfFieldTangents Tangents() {
    HalfFieldTangents t;
    t.x = 1.0f;
    t.y = 0.5625f;
    t.valid = true;
    return t;
}

EngineHeadPose Pose(float yaw, float pitch, float roll, float x = 0.0f, float y = 0.0f,
                    float z = 0.0f) {
    EngineHeadPose p;
    p.yaw = yaw;
    p.pitch = pitch;
    p.roll = roll;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

void CheckOrthonormal(const CameraBasis& b, const char* what) {
    CheckNear(b.forward.Magnitude(), 1.0f, what);
    CheckNear(b.up.Magnitude(), 1.0f, what);
    CheckNear(b.right.Magnitude(), 1.0f, what);
    CheckNear(Vec3::Dot(b.forward, b.up), 0.0f, what);
    CheckNear(Vec3::Dot(b.forward, b.right), 0.0f, what);
    CheckNear(Vec3::Dot(b.up, b.right), 0.0f, what);
    const Vec3 rebuilt = Vec3::Cross(b.up, b.forward);
    CheckNear(Vec3::Dot(rebuilt, b.right), 1.0f, what);
}

// -- the tracker-to-engine boundary ----------------------------------------

// The engine boundary: yaw, pitch, roll and the vertical lean pass through, the
// lateral and forward leans are negated.
//
// Three of those four decisions came from a player looking at the screen, one
// axis at a time. The mod shipped negating yaw, roll and x; yaw and roll came out
// on one report and x came out with them on the theory that the three mirror as a
// block, and then a second report put x back. They do not move as a block here.
void TheEngineBoundaryNegatesTheLateralAndForwardLeans() {
    HeadPose tracker;
    tracker.yaw = 11.0f;
    tracker.pitch = 22.0f;
    tracker.roll = 33.0f;
    tracker.x = 0.11f;
    tracker.y = 0.22f;
    tracker.z = 0.33f;

    const EngineHeadPose e = metroex::ToEngineConvention(tracker);
    CheckNear(e.yaw, 11.0f, "yaw passes through");
    CheckNear(e.pitch, 22.0f, "pitch passes through");
    CheckNear(e.roll, 33.0f, "and roll with it");
    CheckNear(e.x, -0.11f, "the lateral lean is negated, settled by a player after yaw and roll");
    CheckNear(e.y, 0.22f, "vertical lean passes through");
    CheckNear(e.z, -0.33f,
              "the core puts the forward lean on negative z and this engine puts it on +z");
}

// The conversion is a per-axis sign flip and nothing else, so no axis may pick
// up a term from another one.
void TheEngineBoundaryLeavesEveryOtherAxisAlone() {
    HeadPose yawOnly;
    yawOnly.yaw = 40.0f;
    const EngineHeadPose e = metroex::ToEngineConvention(yawOnly);
    CheckNear(e.pitch, 0.0f, "a yaw-only pose leaves pitch at zero");
    CheckNear(e.roll, 0.0f, "and roll");
    CheckNear(e.x, 0.0f, "and x");
    CheckNear(e.y, 0.0f, "and y");
    CheckNear(e.z, 0.0f, "and z");
}

// -- the composition -------------------------------------------------------

void APositiveYawTurnsTheViewToTheRight() {
    const CameraBasis clean = LevelCamera();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(30.0f, 0.0f, 0.0f), true);
    Check(Vec3::Dot(drawn.forward, clean.right) > 0.0f,
          "a positive yaw sends the forward vector toward the clean right vector");
    CheckNear(Vec3::Dot(drawn.forward, clean.forward), std::cos(30.0f * 0.01745329252f),
              "and by the angle it was given");
    CheckNear(drawn.forward.y, 0.0f, "a pure yaw on a level camera stays level");
}

void APositivePitchRaisesTheView() {
    const CameraBasis clean = LevelCamera();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(0.0f, 20.0f, 0.0f), true);
    Check(drawn.forward.y > 0.0f, "a positive pitch raises the forward vector");
    CheckNear(drawn.forward.y, std::sin(20.0f * 0.01745329252f), "by the angle it was given");
    CheckNear(Vec3::Dot(drawn.right, clean.right), 1.0f, "and leaves the right axis alone");
}

void ARollTurnsTheUpAxisAndLeavesTheForwardAxisAlone() {
    const CameraBasis clean = LevelCamera();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(0.0f, 0.0f, 25.0f), true);
    CheckNear(Vec3::Dot(drawn.forward, clean.forward), 1.0f, "roll does not move the forward axis");
    CheckNear(Vec3::Dot(drawn.up, clean.up), std::cos(25.0f * 0.01745329252f),
              "and turns the up axis by the angle it was given");
}

void EveryComposedPoseLeavesAnOrthonormalBasis() {
    const CameraBasis clean = LevelCamera();
    CheckOrthonormal(metroex::ApplyHeadPose(clean, Pose(0.0f, 0.0f, 0.0f), true), "zero pose");
    CheckOrthonormal(metroex::ApplyHeadPose(clean, Pose(35.0f, -22.0f, 17.0f), true),
                     "combined pose, world yaw");
    CheckOrthonormal(metroex::ApplyHeadPose(clean, Pose(35.0f, -22.0f, 17.0f), false),
                     "combined pose, camera yaw");
    CheckOrthonormal(metroex::ApplyHeadPose(PitchedDownCamera(), Pose(40.0f, 10.0f, -30.0f), true),
                     "combined pose on a camera looking straight down");
}

void AZeroPoseChangesNothing() {
    const CameraBasis clean = LevelCamera(Vec3(3.0f, 4.0f, 5.0f));
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(0.0f, 0.0f, 0.0f), true);
    CheckNear(Vec3::Dot(drawn.forward, clean.forward), 1.0f, "forward is unchanged");
    CheckNear(Vec3::Dot(drawn.up, clean.up), 1.0f, "up is unchanged");
    CheckNear((drawn.position - clean.position).Magnitude(), 0.0f, "the eye has not moved");
}

// Looking straight down, world-locked yaw turns about the camera's own forward
// axis: the world spins and the view direction does not move. Camera-locked yaw
// turns about the camera's up, which points along the world horizontal there,
// and swings the view. The two modes being different in exactly this way is the
// reason the toggle exists.
void WorldYawSpinsTheViewWhenLookingStraightDownAndCameraYawSwingsIt() {
    const CameraBasis clean = PitchedDownCamera();
    const CameraBasis world = metroex::ApplyHeadPose(clean, Pose(40.0f, 0.0f, 0.0f), true);
    CheckNear(Vec3::Dot(world.forward, clean.forward), 1.0f,
              "world yaw looking down leaves the view direction where it was");
    Check(Vec3::Dot(world.up, clean.up) < 0.9f, "and spins the frame about it");

    const CameraBasis local = metroex::ApplyHeadPose(clean, Pose(40.0f, 0.0f, 0.0f), false);
    Check(Vec3::Dot(local.forward, clean.forward) < 0.9f,
          "camera yaw looking down swings the view direction");
}

void TheEyeMovesInTheCleanBasisNotTheRotatedOne() {
    const CameraBasis clean = LevelCamera();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(60.0f, 0.0f, 0.0f, 0.3f), true);
    const Vec3 moved = drawn.position - clean.position;
    CheckNear(Vec3::Dot(moved, clean.right), 0.3f, "a lateral lean follows the clean right axis");
    CheckNear(Vec3::Dot(moved, clean.forward), 0.0f, "and picks up nothing from the head turn");
}

void PositionOffsetsAreAppliedOnTheirOwnAxes() {
    const CameraBasis clean = LevelCamera();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(0, 0, 0, 0.1f, 0.2f, 0.3f), true);
    const Vec3 moved = drawn.position - clean.position;
    CheckNear(Vec3::Dot(moved, clean.right), 0.1f, "x runs along right");
    CheckNear(Vec3::Dot(moved, clean.up), 0.2f, "y runs along up");
    CheckNear(Vec3::Dot(moved, clean.forward), 0.3f, "z runs along forward");
}

// -- the reticle -----------------------------------------------------------

// The engine builds the frame's view matrix with the camera basis as its
// columns and its translation row as the negated dot products of the position
// against them, then multiplies by a projection carrying 1/tan of each half
// field. Doing that here by hand, from the same basis, is the only check in
// this file that is not the projection agreeing with itself.
void BuildsTheSameScreenPositionAsTheEnginesOwnMatrices() {
    const CameraBasis clean = LevelCamera(Vec3(10.0f, 2.0f, -4.0f));
    const HalfFieldTangents t = Tangents();
    const CameraBasis drawn =
        metroex::ApplyHeadPose(clean, Pose(18.0f, -11.0f, 9.0f, 0.25f, -0.1f, 0.05f), true);

    const float distance = 7.5f;
    const Vec3 impact = clean.position + clean.forward * distance;

    const Vec3 rel = impact - drawn.position;
    const float viewX = Vec3::Dot(rel, drawn.right);
    const float viewY = Vec3::Dot(rel, drawn.up);
    const float viewZ = Vec3::Dot(rel, drawn.forward);
    // clip = view * projection, with m00 = 1/tanX, m11 = 1/tanY and w = viewZ.
    const float expectedX = (viewX / t.x) / viewZ;
    const float expectedY = (viewY / t.y) / viewZ;

    const AimScreenPoint p =
        metroex::ProjectAimPoint(drawn, clean.position, clean.forward, distance, t);
    Check(p.valid, "a point in front of the eye projects");
    CheckNear(p.ndc_x, expectedX, "the horizontal coordinate matches the matrix path");
    CheckNear(p.ndc_y, expectedY, "the vertical coordinate matches the matrix path");
}

void PureRollLeavesTheReticleAtTheCentre() {
    const CameraBasis clean = LevelCamera();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(0.0f, 0.0f, 35.0f), true);
    const AimScreenPoint p =
        metroex::ProjectAimPoint(drawn, clean.position, clean.forward, 5.0f, Tangents());
    Check(p.valid, "the aim point is in front of the eye");
    CheckNear(p.ndc_x, 0.0f, "pure roll does not move the reticle sideways");
    CheckNear(p.ndc_y, 0.0f, "or vertically");
}

void PurePitchMovesTheReticleVerticallyOnly() {
    const CameraBasis clean = LevelCamera();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(0.0f, 22.0f, 0.0f), true);
    const AimScreenPoint p =
        metroex::ProjectAimPoint(drawn, clean.position, clean.forward, 5.0f, Tangents());
    Check(p.valid, "the aim point is in front of the eye");
    CheckNear(p.ndc_x, 0.0f, "pure pitch does not move the reticle sideways");
    Check(p.ndc_y < -0.1f, "raising the view drops the aim point down the frame");
}

// With roll outermost the projected offset rotates with the roll, because that
// is what the camera does to the frame. The offset keeps its length and turns
// by the roll angle; a projection that ignored roll would leave it where the
// pure-pitch case put it.
void PitchAndRollTogetherRotateTheOffsetWithTheFrame() {
    const CameraBasis clean = LevelCamera();
    const HalfFieldTangents square = [] {
        HalfFieldTangents t;
        t.x = 1.0f;
        t.y = 1.0f;
        t.valid = true;
        return t;
    }();
    const float roll = 30.0f;
    const AimScreenPoint pitchOnly = metroex::ProjectAimPoint(
        metroex::ApplyHeadPose(clean, Pose(0.0f, 15.0f, 0.0f), true), clean.position,
        clean.forward, 5.0f, square);
    const AimScreenPoint both = metroex::ProjectAimPoint(
        metroex::ApplyHeadPose(clean, Pose(0.0f, 15.0f, roll), true), clean.position,
        clean.forward, 5.0f, square);
    Check(pitchOnly.valid && both.valid, "both poses project");

    const float len0 = std::sqrt(pitchOnly.ndc_x * pitchOnly.ndc_x +
                                 pitchOnly.ndc_y * pitchOnly.ndc_y);
    const float len1 = std::sqrt(both.ndc_x * both.ndc_x + both.ndc_y * both.ndc_y);
    CheckNear(len1, len0, "roll does not change how far the reticle is from the centre");

    const float a0 = std::atan2(pitchOnly.ndc_y, pitchOnly.ndc_x);
    const float a1 = std::atan2(both.ndc_y, both.ndc_x);
    float turned = (a1 - a0) * 57.29577951f;
    while (turned > 180.0f) turned -= 360.0f;
    while (turned < -180.0f) turned += 360.0f;
    CheckNear(std::fabs(turned), roll, "and turns it by exactly the roll angle");
}

void WorldYawLookingStraightDownLeavesTheReticleAtTheCentre() {
    const CameraBasis clean = PitchedDownCamera();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(45.0f, 0.0f, 0.0f), true);
    const AimScreenPoint p =
        metroex::ProjectAimPoint(drawn, clean.position, clean.forward, 3.0f, Tangents());
    Check(p.valid, "the aim point is in front of the eye");
    CheckNear(p.ndc_x, 0.0f, "the world spins and the reticle does not move sideways");
    CheckNear(p.ndc_y, 0.0f, "or vertically");
}

// The whole reason the projection takes a distance. A lateral lean of `l`
// against a surface at `d` moves the aim point across the frame by l/d, so
// halving the distance doubles the offset. A reticle built on a fixed depth
// would report the same number twice.
void ALateralLeanMovesTheReticleInInverseProportionToTheDistance() {
    const CameraBasis clean = LevelCamera();
    const HalfFieldTangents t = Tangents();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(0, 0, 0, 0.3f, 0.0f, 0.0f), true);

    const AimScreenPoint close =
        metroex::ProjectAimPoint(drawn, clean.position, clean.forward, 2.0f, t);
    const AimScreenPoint distant =
        metroex::ProjectAimPoint(drawn, clean.position, clean.forward, 4.0f, t);
    Check(close.valid && distant.valid, "both distances project");
    Check(close.ndc_x < -0.01f, "leaning right moves the aim point left across the frame");
    CheckNear(close.ndc_x, distant.ndc_x * 2.0f, "and by twice as much at half the distance");
    CheckNear(close.ndc_y, 0.0f, "a lateral lean does not move it vertically");
}

void AVerticalLeanMovesTheReticleInInverseProportionToTheDistance() {
    const CameraBasis clean = LevelCamera();
    const HalfFieldTangents t = Tangents();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(0, 0, 0, 0.0f, 0.2f, 0.0f), true);

    const AimScreenPoint close =
        metroex::ProjectAimPoint(drawn, clean.position, clean.forward, 1.5f, t);
    const AimScreenPoint distant =
        metroex::ProjectAimPoint(drawn, clean.position, clean.forward, 4.5f, t);
    Check(close.valid && distant.valid, "both distances project");
    Check(close.ndc_y < -0.01f, "rising in the seat moves the aim point down the frame");
    CheckNear(close.ndc_x, 0.0f, "and not sideways");
    CheckNear(close.ndc_y, distant.ndc_y * 3.0f, "by three times as much at a third of the distance");
}

// Opposite leans put the reticle on opposite sides of the frame centre, by the
// same amount, because the point it marks has not moved.
void OppositeLeansAreMirrorImages() {
    const CameraBasis clean = LevelCamera();
    const HalfFieldTangents t = Tangents();
    const AimScreenPoint left = metroex::ProjectAimPoint(
        metroex::ApplyHeadPose(clean, Pose(0, 0, 0, -0.25f, 0, 0), true), clean.position,
        clean.forward, 3.0f, t);
    const AimScreenPoint right = metroex::ProjectAimPoint(
        metroex::ApplyHeadPose(clean, Pose(0, 0, 0, 0.25f, 0, 0), true), clean.position,
        clean.forward, 3.0f, t);
    Check(left.valid && right.valid, "both leans project");
    CheckNear(left.ndc_x, -right.ndc_x, "the two offsets are equal and opposite");
}

void AnAimPointBehindTheEyeIsNotDrawn() {
    const CameraBasis clean = LevelCamera();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(170.0f, 0.0f, 0.0f), true);
    const AimScreenPoint p =
        metroex::ProjectAimPoint(drawn, clean.position, clean.forward, 5.0f, Tangents());
    Check(!p.valid, "a point behind the head-turned view is refused rather than drawn");
}

void AnEdgeOnAimPointIsNotDrawn() {
    const CameraBasis clean = LevelCamera();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(90.0f, 0.0f, 0.0f), true);
    const AimScreenPoint p =
        metroex::ProjectAimPoint(drawn, clean.position, clean.forward, 5.0f, Tangents());
    Check(!p.valid,
          "a point exactly on the plane of the eye is refused instead of dividing by zero");
}

void AnUnusableFieldOfViewIsNotDrawn() {
    const CameraBasis clean = LevelCamera();
    const AimScreenPoint p = metroex::ProjectAimPoint(clean, clean.position, clean.forward, 5.0f,
                                                      HalfFieldTangents{});
    Check(!p.valid, "an invalid pair of half-field tangents refuses the projection");
}

void AnUnusableDistanceIsNotDrawn() {
    const CameraBasis clean = LevelCamera();
    Check(!metroex::ProjectAimPoint(clean, clean.position, clean.forward, 0.0f, Tangents()).valid,
          "a zero distance refuses the projection");
    Check(!metroex::ProjectAimPoint(clean, clean.position, clean.forward, -3.0f, Tangents()).valid,
          "a negative distance refuses the projection");
    const float inf = std::numeric_limits<float>::infinity();
    Check(!metroex::ProjectAimPoint(clean, clean.position, clean.forward, inf, Tangents()).valid,
          "an infinite distance refuses the projection rather than dividing by it");
}

// A definite no-hit is a target at infinity: the eye offset cancels and only
// the direction matters, so a lean must not move the reticle at all.
void ADefiniteNoHitProjectsTheDirectionAndIgnoresTheLean() {
    const CameraBasis clean = LevelCamera();
    const HalfFieldTangents t = Tangents();
    const CameraBasis drawn = metroex::ApplyHeadPose(clean, Pose(12.0f, 0, 0, 0.3f, 0, 0), true);
    const AimScreenPoint p = metroex::ProjectAimDirection(drawn, clean.forward, t);
    const AimScreenPoint noLean = metroex::ProjectAimDirection(
        metroex::ApplyHeadPose(clean, Pose(12.0f, 0, 0), true), clean.forward, t);
    Check(p.valid && noLean.valid, "both project");
    CheckNear(p.ndc_x, noLean.ndc_x, "a lean does not move a target at infinity");
    CheckNear(p.ndc_y, noLean.ndc_y, "in either axis");
}

}  // namespace

int main() {
    TheEngineBoundaryNegatesTheLateralAndForwardLeans();
    TheEngineBoundaryLeavesEveryOtherAxisAlone();

    APositiveYawTurnsTheViewToTheRight();
    APositivePitchRaisesTheView();
    ARollTurnsTheUpAxisAndLeavesTheForwardAxisAlone();
    EveryComposedPoseLeavesAnOrthonormalBasis();
    AZeroPoseChangesNothing();
    WorldYawSpinsTheViewWhenLookingStraightDownAndCameraYawSwingsIt();
    TheEyeMovesInTheCleanBasisNotTheRotatedOne();
    PositionOffsetsAreAppliedOnTheirOwnAxes();

    BuildsTheSameScreenPositionAsTheEnginesOwnMatrices();
    PureRollLeavesTheReticleAtTheCentre();
    PurePitchMovesTheReticleVerticallyOnly();
    PitchAndRollTogetherRotateTheOffsetWithTheFrame();
    WorldYawLookingStraightDownLeavesTheReticleAtTheCentre();
    ALateralLeanMovesTheReticleInInverseProportionToTheDistance();
    AVerticalLeanMovesTheReticleInInverseProportionToTheDistance();
    OppositeLeansAreMirrorImages();
    AnAimPointBehindTheEyeIsNotDrawn();
    AnEdgeOnAimPointIsNotDrawn();
    AnUnusableFieldOfViewIsNotDrawn();
    AnUnusableDistanceIsNotDrawn();
    ADefiniteNoHitProjectsTheDirectionAndIgnoresTheLean();
    return metroex_test::Report();
}
