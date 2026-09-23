#pragma once

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
    // The master toggle is off.
    Disabled,
    // No level is up: the main menu, or a loading screen either side of it.
    NotInGameplay,
    // The tracker has published no pose this frame.
    NoTracker,
};

struct TrackingState {
    TrackingVerdict verdict = TrackingVerdict::Disabled;
    // The sights are up. It never closes the gate: head tracking carries on
    // through an aim, and this only tells the lean to ease out.
    bool aiming = false;
};

// ADS is tested LAST, so a menu, the master toggle or a silent tracker still
// reports its own reason when more than one is true at once - and every earlier
// return leaves `aiming` false, so a stale flag cannot hold the lean out after
// a menu.
//
// `inGameplay` is false ONLY on a positive "no level" reading from the engine.
// Everything GameState cannot read answers true - see game_state.h for why that
// asymmetry is not negotiable on this game.
inline TrackingState DecideTracking(bool enabled, bool inGameplay, bool haveRotation, bool aiming) {
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
    s.verdict = TrackingVerdict::Active;
    return s;
}

inline bool PoseApplies(TrackingVerdict verdict) { return verdict == TrackingVerdict::Active; }

inline const char* TrackingVerdictName(TrackingVerdict verdict) {
    switch (verdict) {
        case TrackingVerdict::Active:        return "active";
        case TrackingVerdict::Disabled:      return "tracking switched off";
        case TrackingVerdict::NotInGameplay: return "no level is up";
        case TrackingVerdict::NoTracker:     return "no tracker data";
    }
    return "unknown";
}

}  // namespace metroex
