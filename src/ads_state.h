#pragma once

#include <cstdint>

namespace metroex {

// Is the player aiming down sights, as the game itself understands it.
//
// Read from the game's own aim flag rather than inferred from the view's zoom or
// field of view. Iron sights do not magnify, so a zoom test answers "not aiming"
// for exactly the weapons a player spends the early game with, and the positional
// lean then keeps moving the eye off the sight line while they try to shoot.
//
// The flag is a static byte in MetroExodus.exe, so it is routed through the
// build-profile registry like every other pinned address here. On a build with no
// matching profile - or a profile written before the address was derived - nothing
// is read and IsAiming() answers false, which is the safe direction: the mod
// behaves exactly as it did before ADS handling existed rather than stranding the
// player in ADS behaviour on a build it cannot read.
class AdsState {
public:
    // Matches the running EXE against the known profiles and logs the outcome.
    void Initialise();

    // Polled, never latched: this re-reads the game's flag on every call. Enter
    // and exit events fire unevenly - an exit that never arrives after firing, a
    // state machine that transitions without an event - and a latched flag that
    // misses one edge either strands the player in ADS behaviour or leaks
    // hip-fire tracking into the aim.
    bool IsAiming() const;

private:
    const volatile uint8_t* m_flag = nullptr;
};

}
