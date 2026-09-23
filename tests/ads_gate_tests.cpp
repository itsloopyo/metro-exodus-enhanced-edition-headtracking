// The verdict walk: whether the head pose reaches the view this frame, and what
// the mod says the reason was.
//
// The sights never close the gate. They are tested last, so a menu still reports
// its own reason when both are true at once, and every earlier return leaves the
// sights flag false: a stale flag through a menu would hold the lean out after
// it.

#include "ads_gate.h"
#include "test_harness.h"

namespace {

using metroex_test::Check;

using metroex::DecideTracking;
using metroex::PoseApplies;
using metroex::TrackingVerdict;

void RaisingTheSightsKeepsTrackingOn() {
    const auto s = DecideTracking(true, true, true, true);
    Check(s.verdict == TrackingVerdict::Active, "the sights up is an ordinary tracked frame");
    Check(s.aiming, "that reports the sights up");
    Check(PoseApplies(s.verdict), "and feeds the pose to the camera");
}

void HipFireIsActive() {
    const auto s = DecideTracking(true, true, true, false);
    Check(s.verdict == TrackingVerdict::Active, "hip fire is an ordinary tracked frame");
    Check(!s.aiming, "with the sights down");
}

void OnlyAnActiveFrameFeedsThePose() {
    Check(PoseApplies(TrackingVerdict::Active), "an active frame feeds the camera");
    Check(!PoseApplies(TrackingVerdict::NotInGameplay), "a menu does not");
    Check(!PoseApplies(TrackingVerdict::Disabled), "nor does the master toggle");
    Check(!PoseApplies(TrackingVerdict::NoTracker), "nor does a tracker with nothing to say");
}

// A menu and the master toggle both outrank the sights in the reported reason,
// and clear the sights flag with it.
void SuppressionOutranksTheSightsAndClearsTheFlag() {
    const auto menu = DecideTracking(true, false, true, true);
    Check(menu.verdict == TrackingVerdict::NotInGameplay, "a menu reports the menu");
    Check(!menu.aiming, "and leaves no stale sights flag behind");

    const auto off = DecideTracking(false, true, true, true);
    Check(off.verdict == TrackingVerdict::Disabled, "the master toggle reports itself");
    Check(!off.aiming, "and leaves no stale sights flag behind");

    const auto silent = DecideTracking(true, true, false, true);
    Check(silent.verdict == TrackingVerdict::NoTracker, "a tracker with nothing to say reports that");
    Check(!silent.aiming, "and leaves no stale sights flag behind");
}

// The state is polled from the game every frame rather than latched, so an exit
// edge that never arrives heals on the next frame.
void TheSightsFlagHealsWithoutAnExitEdge() {
    Check(DecideTracking(true, true, true, true).aiming, "the sights are reported up");
    Check(!DecideTracking(true, true, true, false).aiming,
          "and the next frame that reads them down clears the flag");
}

}  // namespace

int main() {
    RaisingTheSightsKeepsTrackingOn();
    HipFireIsActive();
    OnlyAnActiveFrameFeedsThePose();
    SuppressionOutranksTheSightsAndClearsTheFlag();
    TheSightsFlagHealsWithoutAnExitEdge();

    return metroex_test::Report();
}
