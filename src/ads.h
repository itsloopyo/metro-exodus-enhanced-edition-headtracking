#pragma once

#include "cameraunlock/ads/ads_blend.h"
#include "cameraunlock/ads/ads_fade.h"
#include "cameraunlock/ads/ads_mode.h"
#include "cameraunlock/ads/entry_pose.h"

namespace metroex {

// The shared aim-down-sights module, under this mod's own namespace.
//
// The cycle, its value strings, its toast wording and the shape of the
// transition are a fleet-wide contract rather than a Metro decision: a player
// who learns Insert in one shooter has to find the same slots in the same order
// here. So they live in cameraunlock-core and this file only says which of them
// this game gets.
//
// **Metro Exodus ships all three slots**, not the two-slot shape. The two-slot
// shape needs an aim indicator the mod can move to the clean-aim point while the
// sights are up. The mod does now draw one - see reticle.h - so the trap that
// used to settle this on its own no longer applies, and the optics the game
// hangs off its weapons settle it instead: a scope's own reticle is only honest
// while the eye sits exactly on the optic, which is precisely what head tracking
// breaks. So `marker` earns its slot here - it is the only thing that can say
// where the rounds go once tracking has moved the eye off the sight line.
using cameraunlock::ads::AdsEntryPose;
using cameraunlock::ads::AdsFade;
using cameraunlock::ads::AdsMode;
using cameraunlock::ads::AdsModeLabel;
using cameraunlock::ads::AdsModeToast;
using cameraunlock::ads::AdsModeValue;
using cameraunlock::ads::AdsSuspendsTracking;
using cameraunlock::ads::BlendAdsPose;
using cameraunlock::ads::kDefaultAdsMode;
using cameraunlock::ads::NextAdsMode;
using cameraunlock::ads::ParseAdsMode;

}  // namespace metroex
