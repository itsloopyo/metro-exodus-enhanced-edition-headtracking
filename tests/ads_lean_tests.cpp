// What the camera is fed while the sights are up: the whole rotation, and a lean
// that eases out as they come up and back in as they go down, unless true free
// look is on, when it stays.
//
// Driven through AdsFade frame by frame, the same way TrackingRuntime drives it,
// so a regression in either the easing or the pose it is applied to fails here.

#include "ads.h"
#include "test_harness.h"

namespace {

using metroex_test::Check;

constexpr float kTolerance = 1e-4f;

void CheckNear(float actual, float expected, const char* what) {
    metroex_test::CheckNear(actual, expected, kTolerance, what);
}

metroex::HeadPose MakePose() {
    metroex::HeadPose p;
    p.yaw = 25.0f;
    p.pitch = -12.0f;
    p.roll = 8.0f;
    p.x = 0.20f;
    p.y = -0.05f;
    p.z = -0.30f;
    p.has_position = true;
    return p;
}

void CheckRotationUntouched(const metroex::HeadPose& out, const char* when) {
    const metroex::HeadPose in = MakePose();
    CheckNear(out.yaw, in.yaw, when);
    CheckNear(out.pitch, in.pitch, when);
    CheckNear(out.roll, in.roll, when);
}

void HipFirePassesThePoseThroughUntouched() {
    metroex::AdsFade fade;
    const metroex::HeadPose in = MakePose();
    const metroex::HeadPose out = metroex::EaseLeanForSights(in, fade.Update(false, 0));
    CheckRotationUntouched(out, "at the hip the rotation is the head's");
    CheckNear(out.x, in.x, "and so is the lateral lean");
    CheckNear(out.y, in.y, "and the vertical one");
    CheckNear(out.z, in.z, "and the forward one");
    Check(out.has_position, "and the pose still says it carries a position");
}

void WithTheSightsUpTheRotationStaysAndTheLeanIsGone() {
    metroex::AdsFade fade;
    fade.Update(false, 0);
    fade.Update(true, 10);
    const metroex::HeadPose out = metroex::EaseLeanForSights(
        MakePose(), fade.Update(true, 10 + metroex::AdsFade::kLowerMs));
    CheckRotationUntouched(out, "with the sights up yaw, pitch and roll are absolute and unscaled");
    CheckNear(out.x, 0.0f, "and the lateral lean is gone");
    CheckNear(out.y, 0.0f, "and the vertical one");
    CheckNear(out.z, 0.0f, "and the forward one");
}

void MidTransitionOnlyTheLeanIsScaled() {
    metroex::AdsFade fade;
    fade.Update(false, 0);
    fade.Update(true, 10);
    const float scale = fade.Update(true, 10 + metroex::AdsFade::kLowerMs / 2);
    Check(scale > 0.05f && scale < 0.95f, "half way up the lean is part way out");
    const metroex::HeadPose in = MakePose();
    const metroex::HeadPose out = metroex::EaseLeanForSights(in, scale);
    CheckRotationUntouched(out, "and the rotation is still the head's");
    CheckNear(out.x, in.x * scale, "the lateral lean is scaled by the fade");
    CheckNear(out.y, in.y * scale, "the vertical one by the same amount");
    CheckNear(out.z, in.z * scale, "and the forward one");
}

// A tap of the aim button releases a frame after it was pressed. The lean has to
// turn round from where it had got to rather than from either end.
void AReversalContinuesFromWhereTheLeanWas() {
    metroex::AdsFade fade;
    fade.Update(false, 0);
    fade.Update(true, 10);
    const float mid = fade.Update(true, 10 + metroex::AdsFade::kLowerMs / 2);
    const float reversed = fade.Update(false, 10 + metroex::AdsFade::kLowerMs / 2);
    CheckNear(reversed, mid, "the frame the sights drop on starts where the lean had got to");
    const float next = fade.Update(false, 10 + metroex::AdsFade::kLowerMs / 2 + 16);
    Check(next >= reversed && next - reversed < 0.2f,
          "and it heads back in without a step");
    CheckNear(fade.Update(false, 10 + metroex::AdsFade::kLowerMs + metroex::AdsFade::kRaiseMs),
              1.0f, "arriving back at the full lean");
}

void TheLeanEasesOutOnlyWithTheSightsUpAndFreeLookOff() {
    Check(!metroex::LeanEasesOut(false, false), "sights locked, at the hip: the lean stays");
    Check(!metroex::LeanEasesOut(false, true), "true free look, at the hip: the lean stays");
    Check(metroex::LeanEasesOut(true, false), "sights locked, sights up: the lean eases out");
    Check(!metroex::LeanEasesOut(true, true), "true free look, sights up: the lean stays");
}

void HipFireIsUntouchedInTrueFreeLook() {
    metroex::AdsFade fade;
    const metroex::HeadPose in = MakePose();
    const metroex::HeadPose out =
        metroex::EaseLeanForSights(in, fade.Update(metroex::LeanEasesOut(false, true), 0));
    CheckRotationUntouched(out, "true free look at the hip: the rotation is the head's");
    CheckNear(out.x, in.x, "and so is the lean");
    CheckNear(out.y, in.y, "and so is the lean");
    CheckNear(out.z, in.z, "and so is the lean");
}

void TrueFreeLookWithTheSightsUpPassesThePoseThrough() {
    metroex::AdsFade fade;
    fade.Update(metroex::LeanEasesOut(false, true), 0);
    fade.Update(metroex::LeanEasesOut(true, true), 10);
    const metroex::HeadPose in = MakePose();
    const metroex::HeadPose out = metroex::EaseLeanForSights(
        in, fade.Update(metroex::LeanEasesOut(true, true), 10 + metroex::AdsFade::kLowerMs));
    CheckRotationUntouched(out, "true free look with the sights up: the rotation is the head's");
    CheckNear(out.x, in.x, "and the lateral lean stays in full");
    CheckNear(out.y, in.y, "and the vertical one");
    CheckNear(out.z, in.z, "and the forward one");
}

// Switching to true free look half way through the lean easing out turns the
// fade round from where it had got to, exactly as dropping the sights would.
void ATogglePartWayContinuesFromWhereTheLeanWas() {
    metroex::AdsFade fade;
    fade.Update(metroex::LeanEasesOut(false, false), 0);
    fade.Update(metroex::LeanEasesOut(true, false), 10);
    const float mid = fade.Update(metroex::LeanEasesOut(true, false), 10 + metroex::AdsFade::kLowerMs / 2);
    const float toggled = fade.Update(metroex::LeanEasesOut(true, true), 10 + metroex::AdsFade::kLowerMs / 2);
    CheckNear(toggled, mid, "the frame free look comes on starts where the lean had got to");
    const float next = fade.Update(metroex::LeanEasesOut(true, true), 10 + metroex::AdsFade::kLowerMs / 2 + 16);
    Check(next >= toggled && next - toggled < 0.2f, "and it heads back in without a step");
    CheckNear(fade.Update(metroex::LeanEasesOut(true, true),
                          10 + metroex::AdsFade::kLowerMs + metroex::AdsFade::kRaiseMs),
              1.0f, "arriving at the full lean with the sights still up");
    fade.Update(metroex::LeanEasesOut(true, false), 1000);
    CheckNear(fade.Update(metroex::LeanEasesOut(true, false), 1000 + metroex::AdsFade::kLowerMs), 0.0f,
              "and switching back to sights locked mid-aim eases it out again");
}

}  // namespace

int main() {
    HipFirePassesThePoseThroughUntouched();
    WithTheSightsUpTheRotationStaysAndTheLeanIsGone();
    MidTransitionOnlyTheLeanIsScaled();
    AReversalContinuesFromWhereTheLeanWas();
    TheLeanEasesOutOnlyWithTheSightsUpAndFreeLookOff();
    HipFireIsUntouchedInTrueFreeLook();
    TrueFreeLookWithTheSightsUpPassesThePoseThrough();
    ATogglePartWayContinuesFromWhereTheLeanWas();

    return metroex_test::Report();
}
