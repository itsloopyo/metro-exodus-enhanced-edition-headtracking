#pragma once

#include <cstdint>

#include "head_transform.h"
#include "torch_watch.h"

namespace metroex {

// Turns the player's torch with the head instead of leaving it on the aim.
//
// The doctrine for this lives in cameraunlock/effects/head_follow_light.h, and
// the number with it: the beam LEADS the view by 1.5x rather than matching it,
// because a player who turns their head keeps their eyes on what they turned
// towards, so their gaze sits past the centre of the screen. A beam matched to
// the view alone lands short of what they are actually looking at, and the miss
// is largest exactly when the head is turned furthest - which is when a light is
// most likely to be the reason they turned.
//
// The scaling is applied to the HEAD POSE, through the same composition the
// camera uses (ApplyHeadPose), never to the tracker and never as its own set of
// angles. A beam composed from its own maths is a beam that disagrees with the
// view about which way the head turned.
//
// FINDING the torch is the part that does not generalise, and on 4A it is a
// chain of plain reads lifted from the engine's own `entity_players_torch`
// accessor - see the torch_* fields on BuildProfile for the walk and for the
// fallback offset that looks right and is not. Every "virtual call" in that
// accessor is `mov rax, rcx; ret` behind an adjustor thunk, so nothing here
// invokes game code.
class Torch {
public:
    // Matches the running build and logs the outcome. A profile with no torch
    // addresses leaves the beam alone and says so once.
    void Initialise(bool followsHead, float multiplier, bool watchWrites);

    // Call once per rendered frame, from the camera hook, with the clean camera
    // and the head pose already in engine convention. Writes the led beam
    // direction; the engine recomputes it from the aim next frame, so there is
    // nothing to restore.
    //
    // MEASURED, AND IT DOES NOT WORK FROM HERE. The camera hook runs before the
    // engine's own light update, and that update rewrites the whole basis from
    // the aim: a 60-degree yawed basis forced in from outside and rewritten
    // every 50ms survived 0 of 80 samples, snapping back to the aim each frame.
    // The offsets and the chain are right - a sentinel in the forward is
    // replaced by this mod's own value within a frame - the WRITE POINT is
    // wrong. It has to move to after the engine's light update, or on to
    // whatever the update derives the basis from. See .lab/NOTES.md.
    void ApplyPerFrame(const CameraBasis& clean, const EngineHeadPose& pose, bool worldSpaceYaw);

private:
    // Walks the chain. Returns null whenever any step is unreadable or the
    // engine has not built the torch yet, which is the ordinary state before the
    // player picks the lamp up.
    uintptr_t Resolve() const;

    uintptr_t m_base = 0;
    uint32_t m_rootRva = 0;
    uint32_t m_playerAdjust = 0;
    uint32_t m_subsystemOffset = 0;
    uint32_t m_componentOffset = 0;
    uint32_t m_componentGuard = 0;
    uint32_t m_entityAdjust = 0;
    uint32_t m_narrowRva = 0;
    uint32_t m_matrixOffset = 0;
    bool m_hooked = false;
    uint32_t m_rightOffset = 0;
    uint32_t m_upOffset = 0;
    uint32_t m_dirOffset = 0;
    uint32_t m_posOffset = 0;

    bool m_enabled = false;
    float m_multiplier = 0.0f;
    bool m_loggedFirstWrite = false;

    // Diagnostic only, on the Discovery switch - see torch_watch.h.
    bool m_watchWrites = false;
    TorchWatch m_watch;
};

}  // namespace metroex
