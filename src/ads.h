#pragma once

#include "head_transform.h"

#include "cameraunlock/ads/ads_fade.h"

namespace metroex {

using cameraunlock::ads::AdsFade;

// Head tracking carries straight on through an aim. Raising the sights puts the
// weapon's sight line through the eye, and a head turn rotates the view about
// that same eye, so the sights stay lined up wherever the head points and the
// rounds still land on them. Rotation is therefore never touched here, roll
// included.
//
// The lean is the one part that does not survive the sights coming up: moving
// the eye takes it off the sight line. This mod has no weapon pass of its own to
// draw the weapon from the un-leaned eye, so the lean is eased out while the
// sights are up and back in when they come down. `leanScale` is AdsFade's
// output: 1 at the hip, 0 with the sights up.
inline HeadPose EaseLeanForSights(const HeadPose& pose, float leanScale) {
    HeadPose out = pose;
    out.x *= leanScale;
    out.y *= leanScale;
    out.z *= leanScale;
    return out;
}

}  // namespace metroex
