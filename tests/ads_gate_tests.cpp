// The verdict walk: whether the head pose reaches the view this frame, and what
// the mod says the reason was.
//
// ADS is tested last, so a menu still reports its own reason when both are true
// at once, and every earlier return leaves the sights flag false. A stale flag
// through a menu would keep the render-side marker running against a weapon that
// is not raised.

#include "ads_gate.h"
#include "test_harness.h"

namespace {

using metroex_test::Check;

using metroex::AdsMode;
using metroex::AimMarkerApplies;
using metroex::DecideTracking;
using metroex::PoseApplies;
using metroex::TrackingVerdict;

// `paused` is the one mode that closes the gate on the sights coming up, and it
// still has to report that the sights are up: the gate says whether tracking
// applies, the flag says what the weapon is doing, and the per-frame code needs
// both.
void PausedClosesTheGateAndStillReportsTheSights() {
    const auto s = DecideTracking(true, true, true, true, AdsMode::Paused);
    Check(s.verdict == TrackingVerdict::AdsSuspended, "paused suspends tracking on the sights");
    Check(s.aiming, "and still reports the sights up");
}

// Suspending is an ease-out, not a switch: the pose keeps flowing so the fade can
// run it down, and lowering the weapon eases back rather than swinging the view
// through the whole head angle.
void ASuspendedAimStillFeedsThePose() {
    Check(PoseApplies(TrackingVerdict::AdsSuspended),
          "a suspended aim still feeds the camera, so the fade has something to run down");
    Check(PoseApplies(TrackingVerdict::Active), "and so does an ordinary frame");
    Check(!PoseApplies(TrackingVerdict::NotInGameplay), "a menu does not");
    Check(!PoseApplies(TrackingVerdict::Disabled), "nor does the master toggle");
    Check(!PoseApplies(TrackingVerdict::NoTracker), "nor does a tracker with nothing to say");
}

void TrackedModesKeepTheGateOpen() {
    for (const AdsMode mode : {AdsMode::Marker, AdsMode::Tracked}) {
        const auto s = DecideTracking(true, true, true, true, mode);
        Check(s.verdict == TrackingVerdict::Active, "a tracked mode keeps tracking through the aim");
        Check(s.aiming, "and reports the sights up");
    }
}

void HipFireIsActiveInEveryMode() {
    for (const AdsMode mode : {AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked}) {
        const auto s = DecideTracking(true, true, true, false, mode);
        Check(s.verdict == TrackingVerdict::Active, "hip fire is an ordinary tracked frame");
        Check(!s.aiming, "with the sights down");
    }
}

// A menu and the master toggle both outrank ADS in the reported reason, and
// clear the sights flag with it.
void SuppressionOutranksAdsAndClearsTheFlag() {
    for (const AdsMode mode : {AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked}) {
        const auto menu = DecideTracking(true, false, true, true, mode);
        Check(menu.verdict == TrackingVerdict::NotInGameplay, "a menu reports the menu");
        Check(!menu.aiming, "and leaves no stale sights flag behind");

        const auto off = DecideTracking(false, true, true, true, mode);
        Check(off.verdict == TrackingVerdict::Disabled, "the master toggle reports itself");
        Check(!off.aiming, "and leaves no stale sights flag behind");
    }
}

// No tracker is not an ADS verdict either, and it must not report the sights.
void NoTrackerIsNotAnAdsVerdict() {
    const auto s = DecideTracking(true, true, false, true, AdsMode::Tracked);
    Check(s.verdict == TrackingVerdict::NoTracker, "a tracker with nothing to say reports that");
    Check(!s.aiming, "and leaves no stale sights flag behind");
}

// The state is polled from the game every frame rather than latched, so an exit
// edge that never arrives heals on the next frame instead of stranding the player
// in ADS behaviour.
void TheAdsStateHealsWithoutAnExitEdge() {
    const auto aimed = DecideTracking(true, true, true, true, AdsMode::Paused);
    Check(aimed.verdict == TrackingVerdict::AdsSuspended, "the sights suspend tracking");
    const auto healed = DecideTracking(true, true, true, false, AdsMode::Paused);
    Check(healed.verdict == TrackingVerdict::Active, "and the next frame that reads them down heals");
    Check(!healed.aiming, "with the flag cleared");
}

// A hand-edited file, a key an older release never wrote, or a mode renamed since
// all land on the default rather than on whichever branch happens to be last.
void AnUnknownModeStringIsTheDefault() {
    Check(metroex::ParseAdsMode("paused") == AdsMode::Paused, "paused parses");
    Check(metroex::ParseAdsMode("marker") == AdsMode::Marker, "marker parses");
    Check(metroex::ParseAdsMode("tracked") == AdsMode::Tracked, "tracked parses");
    Check(metroex::ParseAdsMode("  Tracked \r\n") == AdsMode::Tracked,
          "and it is trimmed and case-insensitive");
    Check(metroex::ParseAdsMode("") == metroex::kDefaultAdsMode, "an empty value is the default");
    Check(metroex::ParseAdsMode("sights") == metroex::kDefaultAdsMode, "so is a typo");
    Check(metroex::ParseAdsMode(nullptr) == metroex::kDefaultAdsMode, "so is an absent key");
    Check(metroex::kDefaultAdsMode == AdsMode::Paused, "and the default is paused");
}

// The mark is the difference between the two tracked slots, and the reason
// `paused` is indistinguishable from an unmodded game: with the sights up only
// `marker` draws, and the other two hand the crosshair back to the game.
void OnlyMarkerDrawsTheMarkWithTheSightsUp() {
    const auto aimed = [](AdsMode mode) {
        return DecideTracking(true, true, true, true, mode);
    };

    const auto paused = aimed(AdsMode::Paused);
    Check(!AimMarkerApplies(paused.verdict, paused.aiming, AdsMode::Paused),
          "paused draws nothing, so the sight picture is the game's own");

    const auto marker = aimed(AdsMode::Marker);
    Check(AimMarkerApplies(marker.verdict, marker.aiming, AdsMode::Marker),
          "marker draws the mark the mode is named for");

    const auto tracked = aimed(AdsMode::Tracked);
    Check(!AimMarkerApplies(tracked.verdict, tracked.aiming, AdsMode::Tracked),
          "tracked keeps tracking and draws nothing");
}

// Hip fire is the mod's ordinary reticle compensation, which the ADS cycle has
// no say over: the mark is drawn in all three modes with the sights down.
void HipFireDrawsTheMarkInEveryMode() {
    for (const AdsMode mode : {AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked}) {
        const auto s = DecideTracking(true, true, true, false, mode);
        Check(AimMarkerApplies(s.verdict, s.aiming, mode),
              "the sights down draws the mark whatever the ADS mode says");
    }
}

// A frame the pose never reaches draws no mark either, in every mode. A mark
// left standing says the rounds go somewhere they do not.
void ASuppressedFrameDrawsNoMark() {
    for (const AdsMode mode : {AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked}) {
        Check(!AimMarkerApplies(TrackingVerdict::NotInGameplay, false, mode), "not in a menu");
        Check(!AimMarkerApplies(TrackingVerdict::Disabled, false, mode),
              "nor with tracking switched off");
        Check(!AimMarkerApplies(TrackingVerdict::NoTracker, false, mode),
              "nor with no tracker to project against");
    }
}

// Derived from this frame's inputs and nothing else: the same verdict with the
// sights up and down, or with the mode cycled under it, gives a different answer
// straight away rather than one frame later.
void TheMarkIsDerivedPerFrameRatherThanLatched() {
    Check(AimMarkerApplies(TrackingVerdict::Active, true, AdsMode::Marker),
          "marker, sights up: drawn");
    Check(!AimMarkerApplies(TrackingVerdict::Active, true, AdsMode::Tracked),
          "cycling to tracked on the same frame stops drawing it");
    Check(AimMarkerApplies(TrackingVerdict::Active, false, AdsMode::Tracked),
          "and lowering the weapon on the same frame brings it straight back");
    Check(!AimMarkerApplies(TrackingVerdict::AdsSuspended, true, AdsMode::Paused),
          "a suspended aim still feeds the pose for the fade, and still draws nothing");
}

void TheCycleWalksAllThreeSlotsInOrder() {
    Check(metroex::NextAdsMode(AdsMode::Paused) == AdsMode::Marker, "paused cycles to marker");
    Check(metroex::NextAdsMode(AdsMode::Marker) == AdsMode::Tracked, "marker cycles to tracked");
    Check(metroex::NextAdsMode(AdsMode::Tracked) == AdsMode::Paused, "and tracked back to paused");
}

}  // namespace

int main() {
    PausedClosesTheGateAndStillReportsTheSights();
    ASuspendedAimStillFeedsThePose();
    TrackedModesKeepTheGateOpen();
    HipFireIsActiveInEveryMode();
    SuppressionOutranksAdsAndClearsTheFlag();
    NoTrackerIsNotAnAdsVerdict();
    TheAdsStateHealsWithoutAnExitEdge();
    OnlyMarkerDrawsTheMarkWithTheSightsUp();
    HipFireDrawsTheMarkInEveryMode();
    ASuppressedFrameDrawsNoMark();
    TheMarkIsDerivedPerFrameRatherThanLatched();
    AnUnknownModeStringIsTheDefault();
    TheCycleWalksAllThreeSlotsInOrder();

    return metroex_test::Report();
}
