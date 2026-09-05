#pragma once

#include <cstdint>

namespace metroex {

// Tells a level from the main menu, so head tracking stays out of the way where
// the frame is not the player looking around.
//
// One FLAGS byte in the engine decides it, reached through a pointer chain the
// mod does not own, so every step is checked before the next is taken - including
// the vtable comparison the engine itself makes before trusting the object's
// layout, since the field only exists on one of the classes that can sit there.
//
// THE MENU TEST IS THE PLAYER'S EQUIPMENT COMPONENT, and it is deliberately not
// anything to do with levels or rendering. Measured:
//
//   main menu    component absent  (guard 0,  component null)
//   in a level   component present (guard 32, component set)
//
// Three earlier signals were tried and all failed the same way - they described
// the level or the frame rather than the player:
//
//   engine level flags   read "no level" in a TUNNEL during normal gameplay
//   render aspect        reads the menu's 1.5 during letterboxed cinematics
//   "a player exists"    resolves at the main menu too
//
// A streaming boundary does not take the player's equipment away, which is why
// this one holds where those did not. Before trusting any replacement, force the
// tunnel condition into a live level - write the level flags to "no level" and
// see whether the mod suppresses - because that is what caught the aspect.
//
// ONLY A POSITIVE "no level" READING SUPPRESSES. A chain that cannot be walked,
// a class this mod does not know, and a flags combination it has never seen all
// leave tracking ON. That asymmetry is not caution for its own sake, it is the
// scar from the field this one replaces: +0x6BC reads "no level" while the
// player walks around a level entered from a SAVE, which is how most people
// play, and the mod suppressed head tracking for whole sessions with nothing in
// the log to say why.
//
// SO: DO NOT RE-POINT THIS AT A FIELD THAT HAS ONLY BEEN CHECKED WITH -map.
// The offset in use was kept only because it agreed across three independent
// menus and three levels covering both entry paths, AND flipped back on a quit
// to the main menu. Six other candidates passed everything except that last
// test - they held their level value at the menu, because they were level
// residue rather than state.
//
// THE PAUSE MENU IS COVERED TOO, by a second byte in the same object read under
// the same vtable guard. The level flag alone cannot see a pause - it reads
// `level` straight through one.
//
// WHAT IS STILL NOT COVERED: the journal and the death screen, neither of which
// has been measured. Head tracking stays live behind them. It costs the player a
// view that drifts under a screen they are reading; it costs them nothing they
// can aim or walk into, because the camera the game reads is put back clean
// every frame either way.
class GameState {
public:
    // Matches the running EXE against the known profiles and logs the outcome.
    void Initialise();

    // Call once per rendered frame. Walks the chain at most every kPollFrames
    // frames: a chain that ever points at unmapped memory raises and swallows an
    // access violation on every call, which is a frame-time cliff with nothing in
    // the log to explain it.
    void Update();


    bool IsInGameplay() const { return m_inGameplay; }

private:
    // How far the chain got this poll. Everything except `Read` means the state
    // is UNKNOWN, and unknown leaves head tracking on - see the comment on
    // Update(). `None` is the initial value, so the first poll always reports.
    enum class Chain {
        None,
        Read,
        NoGameObject,
        NoModeObject,
        NoVtable,
        ForeignMode,
        NoStateByte,
    };

    void Unknown(Chain chain, uintptr_t detail);
    void Report(Chain chain, uint8_t state, uint8_t pause);
    bool PlayerComponentExists() const;

    // How many frames between two walks of the chain, which is the cadence
    // AGENTS.md asks for. Frame-counted rather than timed, so it is a second at
    // 30fps and a fifth of a second at 144: a menu the player has just opened is
    // suppressed well inside the time it takes them to read anything.
    //
    // The cost is that state 1 - loading, which the engine only ever holds
    // transiently - is often stepped over, so the boot sequence in the log reads
    // `main menu` then `in a level` rather than naming the load between them.
    static constexpr uint32_t kPollFrames = 30;

    // The global the chain starts at, the two offsets that walk it, and the
    // vtable the object at the end has to have before its byte means anything.
    // Held rather than re-read from the profile each frame so Update() is a
    // handful of loads and two compares.
    uintptr_t m_root = 0;
    uint32_t m_objectOffset = 0;
    uint32_t m_flagOffset = 0;
    uintptr_t m_vtable = 0;
    uint32_t m_pauseOffset = 0;

    // The walk to the player's equipment component, which is the menu test.
    uintptr_t m_playerRoot = 0;
    uint32_t m_playerAdjust = 0;
    uint32_t m_subsystemOffset = 0;
    uint32_t m_componentOffset = 0;
    uint32_t m_componentGuard = 0;
    bool m_lastComponent = false;

    // MetroExodus.exe's base, kept only so the "game mode I cannot read" line
    // can report the foreign vtable as an RVA. An absolute address moves with
    // ASLR between two runs and is no use in a bug report; an RVA is the thing
    // a new profile field would be written from.
    uintptr_t m_base = 0;

    // The last foreign vtable reported, so a mode the mod cannot read logs once
    // rather than every poll for as long as it is up.
    uintptr_t m_lastForeignVtable = 0;

    uint32_t m_framesSincePoll = kPollFrames;

    bool m_inGameplay = true;

    Chain m_lastChain = Chain::None;
    uint8_t m_lastState = 0;
    uint8_t m_lastPause = 0;
};

}
