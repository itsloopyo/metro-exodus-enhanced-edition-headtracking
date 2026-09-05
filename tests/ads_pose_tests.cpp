// What pose the camera is fed while the sights are up.
//
// The entry pose is what makes the tracked ADS modes swing onto the aim point
// and then keep tracking from there, and it is the one piece of the shared ADS
// module whose failures are invisible from a settings or a gate test: a seam
// crossing whips the view a full turn the wrong way, and a capture taken off a
// stale rotation holds the whole aim at an offset. The primitives live in
// cameraunlock-core; these are the cases this mod would have to notice a
// regression in.

#include "ads.h"
#include "test_harness.h"

namespace {

using metroex_test::Check;

// Degrees and metres off an interpolated transition, so the slack is a hundredth
// of a degree rather than a rounding step.
constexpr float kTolerance = 1e-4f;

void CheckNear(float actual, float expected, const char* what) {
    metroex_test::CheckNear(actual, expected, kTolerance, what);
}

metroex::AdsEntryPose::Pose MakePose(float pitch, float yaw, float roll, float x = 0.0f,
                                     float y = 0.0f, float z = 0.0f) {
    metroex::AdsEntryPose::Pose p;
    p.pitch = pitch;
    p.yaw = yaw;
    p.roll = roll;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

// ---- entry pose ------------------------------------------------------------

void HipFirePassesTheAbsolutePoseThrough() {
    metroex::AdsEntryPose entry;
    const auto out = entry.Relative(false, true, MakePose(5.0f, 20.0f, 3.0f, 0.1f, 0.2f, -0.3f));
    CheckNear(out.pitch, 5.0f, "hip fire keeps pitch");
    CheckNear(out.yaw, 20.0f, "hip fire keeps yaw");
    CheckNear(out.roll, 3.0f, "hip fire keeps roll");
    CheckNear(out.z, -0.3f, "hip fire keeps position");
    Check(!entry.HasEntry(), "hip fire holds no entry pose");
}

void TheEntryFrameIsIdentity() {
    metroex::AdsEntryPose entry;
    const auto out = entry.Relative(true, true, MakePose(5.0f, 20.0f, 3.0f, 0.1f, 0.2f, -0.3f));
    CheckNear(out.pitch, 0.0f, "the frame the sights came up on has zero pitch");
    CheckNear(out.yaw, 0.0f, "the frame the sights came up on has zero yaw");
    CheckNear(out.x, 0.0f, "the frame the sights came up on has zero x");
    CheckNear(out.y, 0.0f, "the frame the sights came up on has zero y");
    CheckNear(out.z, 0.0f, "the frame the sights came up on has zero z");
    Check(entry.HasEntry(), "the entry pose is held for the rest of the aim");
}

// Roll moves no aim point, so zeroing it would yank a tilt the player is holding
// back to level on the way in and lean it back in on the way out.
void RollStaysAbsolute() {
    metroex::AdsEntryPose entry;
    entry.Relative(true, true, MakePose(0.0f, 0.0f, 12.0f));
    const auto out = entry.Relative(true, true, MakePose(0.0f, 0.0f, 15.0f));
    CheckNear(out.roll, 15.0f, "roll is never made relative to the entry frame");
}

// Yaw arrives wrapped into -180..180, so a plain subtraction reads a 10 degree
// move across the seam as -350 and whips the view a full turn the wrong way.
void YawCrossesTheSeamTheShortWay() {
    metroex::AdsEntryPose entry;
    entry.Relative(true, true, MakePose(0.0f, 175.0f, 0.0f));
    const auto out = entry.Relative(true, true, MakePose(0.0f, -175.0f, 0.0f));
    CheckNear(out.yaw, 10.0f, "175 to -175 is a 10 degree move, not -350");

    metroex::AdsEntryPose back;
    back.Relative(true, true, MakePose(0.0f, -175.0f, 0.0f));
    const auto other = back.Relative(true, true, MakePose(0.0f, 175.0f, 0.0f));
    CheckNear(other.yaw, -10.0f, "and the same the other way round");
}

void PitchAndPositionAreRelative() {
    metroex::AdsEntryPose entry;
    entry.Relative(true, true, MakePose(5.0f, 0.0f, 0.0f, 0.10f, 0.20f, -0.30f));
    const auto out = entry.Relative(true, true, MakePose(9.0f, 0.0f, 0.0f, 0.15f, 0.05f, -0.10f));
    CheckNear(out.pitch, 4.0f, "pitch is measured from the entry frame");
    CheckNear(out.x, 0.05f, "x is measured from the entry frame");
    CheckNear(out.y, -0.15f, "y is measured from the entry frame");
    CheckNear(out.z, 0.20f, "z is measured from the entry frame");
}

// Interpolators are reset on suppressed frames and publish nothing until a fresh
// packet lands. Capturing then freezes a pre-suppression pose and holds the whole
// aim at that offset: aim, open a menu, move your head, come back with the sights
// still up.
void CaptureWaitsForALiveRotation() {
    metroex::AdsEntryPose entry;
    const auto dead = entry.Relative(true, false, MakePose(5.0f, 20.0f, 0.0f));
    Check(!entry.HasEntry(), "a dead rotation captures no entry pose");
    CheckNear(dead.yaw, 20.0f, "and passes the pose through untouched meanwhile");

    const auto live = entry.Relative(true, true, MakePose(7.0f, 30.0f, 0.0f));
    Check(entry.HasEntry(), "the first live rotation is what captures");
    CheckNear(live.yaw, 0.0f, "and that frame is the identity");
}

void LoweringTheWeaponDropsTheEntry() {
    metroex::AdsEntryPose entry;
    entry.Relative(true, true, MakePose(5.0f, 20.0f, 0.0f));
    const auto hip = entry.Relative(false, true, MakePose(5.0f, 20.0f, 0.0f));
    Check(!entry.HasEntry(), "lowering the weapon drops the entry pose");
    CheckNear(hip.yaw, 20.0f, "so the view swings back by the angle the head is holding");
}

// ---- blend -----------------------------------------------------------------

void AtTheHipEveryModeIsTheHeadPose() {
    const auto absolute = MakePose(5.0f, 20.0f, 3.0f, 0.1f, 0.2f, -0.3f);
    const auto relative = MakePose(1.0f, 2.0f, 3.0f);
    for (const metroex::AdsMode mode :
         {metroex::AdsMode::Paused, metroex::AdsMode::Marker, metroex::AdsMode::Tracked}) {
        const auto out = metroex::BlendAdsPose(mode, 1.0f, absolute, relative);
        CheckNear(out.yaw, 20.0f, "scale 1 is the head pose, whatever the mode");
        CheckNear(out.z, -0.3f, "including position");
    }
}

void PausedKeepsRollAndDropsTheRest() {
    const auto absolute = MakePose(5.0f, 20.0f, 3.0f, 0.1f, 0.2f, -0.3f);
    const auto out =
        metroex::BlendAdsPose(metroex::AdsMode::Paused, 0.0f, absolute, MakePose(0.0f, 0.0f, 0.0f));
    CheckNear(out.pitch, 0.0f, "paused hands the pitch back to the game");
    CheckNear(out.yaw, 0.0f, "paused hands the yaw back to the game");
    CheckNear(out.x, 0.0f, "paused hands the lean back to the game");
    CheckNear(out.z, 0.0f, "paused hands the whole lean back to the game");
    CheckNear(out.roll, 3.0f, "a head tilt survives, because it moves no aim point");
}

void TrackedModesLandOnTheEntryRelativePose() {
    const auto absolute = MakePose(5.0f, 20.0f, 3.0f, 0.1f, 0.2f, -0.3f);
    const auto relative = MakePose(1.0f, 2.0f, 3.0f, 0.01f, 0.02f, -0.03f);
    for (const metroex::AdsMode mode : {metroex::AdsMode::Marker, metroex::AdsMode::Tracked}) {
        const auto out = metroex::BlendAdsPose(mode, 0.0f, absolute, relative);
        CheckNear(out.pitch, 1.0f, "a tracked mode settles on the entry-relative pitch");
        CheckNear(out.yaw, 2.0f, "a tracked mode settles on the entry-relative yaw");
        CheckNear(out.z, -0.03f, "a tracked mode settles on the entry-relative lean");
        CheckNear(out.roll, 3.0f, "and roll is absolute in every mode");
    }
}

// ---- fade ------------------------------------------------------------------

void TheTransitionEasesRatherThanSwitching() {
    metroex::AdsFade fade;
    CheckNear(fade.Update(false, 0), 1.0f, "the hip is scale 1");
    CheckNear(fade.Update(true, 0), 1.0f, "the frame the sights start coming up on is still full");
    const float mid = fade.Update(true, metroex::AdsFade::kLowerMs / 2);
    Check(mid > 0.05f && mid < 0.95f, "the sights coming up ease the pose off rather than cut it");
    CheckNear(fade.Update(true, metroex::AdsFade::kLowerMs), 0.0f, "and arrive at nothing");
}

// A tap of the aim button releases a frame after it was pressed, which is the
// case a held aim never reaches: the reversal has to start from where the
// transition is, not from the end the interrupted leg was heading for.
void AReversalStartsFromWhereTheTransitionIs() {
    metroex::AdsFade fade;
    fade.Update(false, 0);
    const float pressed = fade.Update(true, 1);
    const float released = fade.Update(false, 2);
    Check(released > 0.9f, "a one-frame tap does not remove a fully applied pose");
    Check(released >= pressed - 0.05f, "and the reversal picks up from where the leg had got to");
}

void SuppressionDropsTheTransition() {
    metroex::AdsFade fade;
    fade.Update(true, 0);
    fade.Update(true, metroex::AdsFade::kLowerMs);
    fade.Reset();
    CheckNear(fade.Update(true, metroex::AdsFade::kLowerMs + 1), 1.0f,
              "a reset drops back to the hip, so the next aim eases in again");
}

}  // namespace

int main() {
    HipFirePassesTheAbsolutePoseThrough();
    TheEntryFrameIsIdentity();
    RollStaysAbsolute();
    YawCrossesTheSeamTheShortWay();
    PitchAndPositionAreRelative();
    CaptureWaitsForALiveRotation();
    LoweringTheWeaponDropsTheEntry();

    AtTheHipEveryModeIsTheHeadPose();
    PausedKeepsRollAndDropsTheRest();
    TrackedModesLandOnTheEntryRelativePose();

    TheTransitionEasesRatherThanSwitching();
    AReversalStartsFromWhereTheTransitionIs();
    SuppressionDropsTheTransition();

    return metroex_test::Report();
}
