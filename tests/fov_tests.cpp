// The half-field tangents every projection in the mod divides by.
//
// The engine keeps a vertical field of view in degrees and a separate
// horizontal multiplier, and builds its own screen-to-world ray as
// fwd*n + up*(ndcY*n*tanY) + right*(ndcX*n*tanY*aspect). These checks pin the
// same relationship, so a reticle drawn with them lands where the engine's own
// unprojection says it should.

#include <limits>

#include "fov.h"
#include "test_harness.h"

namespace {

using metroex_test::Check;

// Loose enough to absorb a float tangent of a float half-angle in radians, tight
// enough that a wrong degrees-to-radians constant or a missing halving fails.
constexpr float kTolerance = 1e-5f;

void CheckNear(float actual, float expected, const char* what) {
    metroex_test::CheckNear(actual, expected, kTolerance, what);
}

// 90 degrees is the one field of view whose half-field tangent is exactly 1, so
// it pins the degrees-to-radians conversion and the halving without leaning on
// a number copied out of the implementation.
void NinetyDegreesVerticalGivesAUnitVerticalTangent() {
    const metroex::HalfFieldTangents t = metroex::TangentsFromCameraFov(90.0f, 1.0f);
    Check(t.valid, "90 degrees at 1:1 is a usable projection");
    CheckNear(t.y, 1.0f, "tan(90/2) is 1");
    CheckNear(t.x, 1.0f, "and a 1:1 aspect leaves the horizontal half-field the same");
}

void TheHorizontalHalfFieldIsTheVerticalOneTimesTheAspect() {
    const metroex::HalfFieldTangents wide = metroex::TangentsFromCameraFov(60.0f, 16.0f / 9.0f);
    const metroex::HalfFieldTangents square = metroex::TangentsFromCameraFov(60.0f, 1.0f);
    Check(wide.valid && square.valid, "both aspects are usable projections");
    CheckNear(wide.y, square.y,
              "widening the picture leaves the vertical half-field alone, which is what makes "
              "this engine Hor+");
    CheckNear(wide.x, square.y * (16.0f / 9.0f), "and scales the horizontal one by the aspect");
}

// The aiming coefficient multiplies the base field of view, so raising a scope
// is a smaller number reaching this function, not a separate code path. A
// reticle that read a fixed field of view would sit still while the picture
// narrowed around it.
void NarrowingTheFieldOfViewNarrowsBothHalfFields() {
    const metroex::HalfFieldTangents hip = metroex::TangentsFromCameraFov(60.0f, 16.0f / 9.0f);
    const metroex::HalfFieldTangents scoped = metroex::TangentsFromCameraFov(15.0f, 16.0f / 9.0f);
    Check(scoped.valid, "a scoped field of view is a usable projection");
    Check(scoped.y < hip.y, "the vertical half-field shrinks with the field of view");
    Check(scoped.x < hip.x, "and so does the horizontal one");
}

// Before the game has drawn a frame the camera globals are still zero, and a
// projection built from them would put the reticle at the origin of a divide by
// zero rather than nowhere. Every consumer keys off `valid`.
void AnUndrawnOrImpossibleFrameIsNotAProjection() {
    Check(!metroex::TangentsFromCameraFov(0.0f, 1.777f).valid,
          "a zero field of view is not a projection");
    Check(!metroex::TangentsFromCameraFov(60.0f, 0.0f).valid, "and neither is a zero aspect");
    Check(!metroex::TangentsFromCameraFov(-60.0f, 1.777f).valid,
          "nor a negative field of view");
    Check(!metroex::TangentsFromCameraFov(180.0f, 1.777f).valid,
          "nor 180 degrees, where the half-field tangent is infinite");
    Check(!metroex::TangentsFromCameraFov(std::numeric_limits<float>::quiet_NaN(), 1.777f).valid,
          "nor a NaN field of view, which a plain `<=` comparison would let through");
    Check(!metroex::TangentsFromCameraFov(60.0f, std::numeric_limits<float>::quiet_NaN()).valid,
          "nor a NaN aspect");
}

// The engine clamps the camera field of view to 0.01 and 179 degrees, so both
// ends of that interval reach this function in a running game.
void TheEndsOfTheEnginesOwnClampAreProjections() {
    const metroex::HalfFieldTangents narrow = metroex::TangentsFromCameraFov(0.01f, 1.777f);
    const metroex::HalfFieldTangents wide = metroex::TangentsFromCameraFov(179.0f, 1.777f);
    Check(narrow.valid, "0.01 degrees is a usable projection");
    Check(wide.valid, "and so is 179 degrees");
    Check(wide.y > narrow.y, "the wider one has the larger half-field");
}

}  // namespace

int main() {
    NinetyDegreesVerticalGivesAUnitVerticalTangent();
    TheHorizontalHalfFieldIsTheVerticalOneTimesTheAspect();
    NarrowingTheFieldOfViewNarrowsBothHalfFields();
    AnUndrawnOrImpossibleFrameIsNotAProjection();
    TheEndsOfTheEnginesOwnClampAreProjections();

    return metroex_test::Report();
}
