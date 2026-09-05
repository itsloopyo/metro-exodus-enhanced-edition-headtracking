#pragma once

#include "ads.h"

namespace metroex {

// Whether the head pose reaches the view this frame, and why not when it does
// not.
//
// A pure function, kept out of the per-frame code so the whole walk can be
// exercised without the game. Every answer it gives is a frame the player either
// sees their head in or does not, and none of that is reachable from a test with
// a game in the loop.
enum class TrackingVerdict {
    // The head pose is applied in full.
    Active,
    // The sights are up in `paused` mode. The pose is still fed to the camera,
    // because it is being EASED off rather than switched off - see AdsFade - and
    // once it has gone the frame is the frame the game would have drawn on its
    // own, bar the head tilt: roll is left out of the fade in every mode, since
    // it moves neither the eye off the barrel nor the aim off the middle of the
    // frame (cameraunlock/ads/ads_blend.h).
    AdsSuspended,
    // The master toggle is off.
    Disabled,
    // No level is up: the main menu, or a loading screen either side of it.
    NotInGameplay,
    // The tracker has published no pose this frame.
    NoTracker,
};

struct TrackingState {
    TrackingVerdict verdict = TrackingVerdict::Disabled;
    // The sights are up. Reported in EVERY mode, including `paused` where the
    // gate is closed: the gate says whether tracking applies, this says what the
    // weapon is doing, and the per-frame code needs both - the aim marker is
    // placed from it.
    bool aiming = false;
};

// ADS is tested LAST, so a menu, the master toggle or a silent tracker still
// reports its own reason when more than one is true at once - and every earlier
// return leaves `aiming` false, because a stale flag through a menu would keep
// the render-side marker running against a weapon that is not raised.
//
// `inGameplay` is false ONLY on a positive "no level" reading from the engine.
// Everything GameState cannot read answers true - see game_state.h for why that
// asymmetry is not negotiable on this game.
inline TrackingState DecideTracking(bool enabled, bool inGameplay, bool haveRotation, bool aiming,
                                    AdsMode mode) {
    TrackingState s;
    if (!enabled) {
        s.verdict = TrackingVerdict::Disabled;
        return s;
    }
    if (!inGameplay) {
        s.verdict = TrackingVerdict::NotInGameplay;
        return s;
    }
    if (!haveRotation) {
        s.verdict = TrackingVerdict::NoTracker;
        return s;
    }
    s.aiming = aiming;
    s.verdict = (aiming && AdsSuspendsTracking(mode)) ? TrackingVerdict::AdsSuspended
                                                      : TrackingVerdict::Active;
    return s;
}

// A pose is fed to the camera in both of the first two verdicts. AdsSuspended
// needs it because suspending is an ease-out, not a switch: dropping the pose on
// the falling edge into ADS would throw away the smoothing state, and lowering
// the weapon would then swing the view back through the whole head angle, dozens
// of times a firefight. Once the fade has run down, the pose is zero and writing
// it is the frame the game would have drawn anyway, so the camera path must be
// able to write a zero pose rather than needing to be handed the camera back.
inline bool PoseApplies(TrackingVerdict verdict) {
    return verdict == TrackingVerdict::Active || verdict == TrackingVerdict::AdsSuspended;
}

// Whether the mod draws its own aim mark this frame, and with it whether the
// game's own crosshair stays hidden.
//
// Derived here from the verdict, the sights and the mode on every frame, never
// latched. A mark left standing from the frame before is a mark saying the
// rounds go somewhere they no longer do, which is worse than no mark at all.
//
// Hip fire draws it in every mode: that is the mod's ordinary reticle
// compensation and the ADS cycle has no say over it. With the sights up only
// `marker` draws. `paused` owes the player a frame indistinguishable from an
// unmodded game, and `tracked` is the mode whose whole point is a clean screen,
// so both hand the crosshair back to the game and let the sight picture stand on
// its own.
inline bool AimMarkerApplies(TrackingVerdict verdict, bool aiming, AdsMode mode) {
    if (!PoseApplies(verdict)) return false;
    if (!aiming) return true;
    return mode == AdsMode::Marker;
}

inline const char* TrackingVerdictName(TrackingVerdict verdict) {
    switch (verdict) {
        case TrackingVerdict::Active:        return "active";
        case TrackingVerdict::AdsSuspended:  return "sights up, tracking paused";
        case TrackingVerdict::Disabled:      return "tracking switched off";
        case TrackingVerdict::NotInGameplay: return "no level is up";
        case TrackingVerdict::NoTracker:     return "no tracker data";
    }
    return "unknown";
}

}  // namespace metroex
