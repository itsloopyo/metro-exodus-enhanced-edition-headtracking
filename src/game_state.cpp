#include "game_state.h"

#include "build_profile.h"
#include "logging.h"

#include "cameraunlock/memory/safe_memory.h"

namespace metroex {

namespace {

// The flags byte at the end of the chain. Bit 0 set means a level is up, bit 1
// set means none is. Both bits, or neither, is a combination this mod has never
// read back and is treated as UNKNOWN.
//
// Only a positive "no level" reading suppresses. Unknown does not, and neither
// does an unwalkable chain - see the comment on Update(). That asymmetry is the
// whole lesson of this file: the previous field, +0x6BC, read "no level" while
// the player walked around a level entered from a save, and the mod switched
// head tracking off for entire sessions with nothing in the log. A view that
// drifts under a menu is visible and survivable; a mod that silently does
// nothing is not.
constexpr uint8_t kLevelBit = 0x01;
constexpr uint8_t kNoLevelBit = 0x02;
constexpr uint8_t kStateBits = kLevelBit | kNoLevelBit;

enum class Level { Up, None, Unknown };

// The pause byte. 1 is paused, 0 is not, and anything else is a value this mod
// has never read back - which suppresses nothing, for the same reason everything
// else here fails open.
constexpr uint8_t kPaused = 1;
constexpr uint8_t kNotPaused = 0;

enum class Pause { Yes, No, Unknown };

Level ReadLevel(uint8_t flags) {
    switch (flags & kStateBits) {
        case kLevelBit:
            return Level::Up;
        case kNoLevelBit:
            return Level::None;
        default:
            break;
    }
    return Level::Unknown;
}

Pause ReadPause(uint8_t flag) {
    if (flag == kPaused) return Pause::Yes;
    if (flag == kNotPaused) return Pause::No;
    return Pause::Unknown;
}

// THE MENU TEST: does the player have an equipment component.
//
// Measured, same build, same session:
//
//   main menu    component absent  (guard 0,  component null)
//   in a level   component present (guard 32, component set)
//
// It is a property of the PLAYER, not of level streaming and not of rendering,
// which is the whole reason to use it. Three signals were tried and each failed
// on that axis:
//
//   engine level flags  read "no level" in a tunnel during normal gameplay
//   render aspect       reads the menu's 1.5 during letterboxed cinematics too
//   "a player exists"   resolves at the main menu as well
//
// The aspect one was caught by forcing the tunnel condition into a live level -
// flags to 0xC2 with the aspect at 1.5 - and watching the mod suppress. Do that
// again before trusting any future candidate.
const char* StateName(uint8_t flags) {
    switch (ReadLevel(flags)) {
        case Level::Up:
            return "the engine reports a level";
        case Level::None:
            return "the engine reports no level (not acted on)";
        case Level::Unknown:
            break;
    }
    return "a flags combination this mod does not know";
}

// The newest profile the mod knows about labels which way a build mismatch runs,
// which is the difference between "update the mod" and "let the store finish
// updating the game".
const char* MismatchDirection(const cameraunlock::memory::PeFingerprint& running) {
    switch (cameraunlock::memory::ClassifyMismatch(running, FingerprintOf(kKnownProfiles[0]))) {
        case cameraunlock::memory::FingerprintMismatch::Newer:
            return "newer than any build this mod knows about - check the releases page for an "
                   "update";
        case cameraunlock::memory::FingerprintMismatch::Older:
            return "older than any build this mod knows about";
        case cameraunlock::memory::FingerprintMismatch::Differs:
            break;
    }
    return "the same age as a known build but a different binary - a modified or repacked EXE";
}

}  // namespace

void GameState::Initialise() {
    const ResolvedBuild build = ResolveRunningBuild();

    // This is the one subsystem that says which way a build mismatch runs - the
    // difference between "update the mod" and "let the store finish updating the
    // game" - so it words that outcome itself rather than through the cause
    // clause the other readers share.
    if (build.outcome == BuildLookup::NoMatchingProfile) {
        Log::Line("Game state: this MetroExodus.exe is %s (TimeDateStamp 0x%08X, SizeOfImage "
                  "0x%08X, CheckSum 0x%08X). Head tracking is unaffected - nothing on this "
                  "game is gated on the game state.",
                  MismatchDirection(build.fingerprint), build.fingerprint.TimeDateStamp,
                  build.fingerprint.SizeOfImage, build.fingerprint.CheckSum);
        return;
    }
    if (const char* cause = BuildLookupCause(build.outcome)) {
        Log::Line("Game state: %s; head tracking is unaffected", cause);
        return;
    }

    const BuildProfile& p = *build.profile;
    if (p.level_state_root_rva == 0 || p.level_state_vtable_rva == 0) {
        Log::Line("Game state: build %s is recognised but its state address has not been "
                  "derived; nothing to report each frame",
                  p.name);
        return;
    }
    if (!RvaFits(p.level_state_root_rva, sizeof(void*), build.fingerprint.SizeOfImage) ||
        !RvaFits(p.level_state_vtable_rva, sizeof(void*), build.fingerprint.SizeOfImage)) {
        Log::Line("ERROR: build profile %s has a state address outside the image; the game "
                  "state will not be read",
                  p.name);
        return;
    }

    m_root = reinterpret_cast<uintptr_t>(build.base) + p.level_state_root_rva;
    m_objectOffset = p.level_state_object_offset;
    m_flagOffset = p.level_state_flag_offset;
    m_base = reinterpret_cast<uintptr_t>(build.base);
    m_vtable = m_base + p.level_state_vtable_rva;
    m_pauseOffset = p.pause_flag_offset;
    if (p.torch_root_rva != 0 &&
        RvaFits(p.torch_root_rva, sizeof(void*), build.fingerprint.SizeOfImage)) {
        m_playerRoot = m_base + p.torch_root_rva;
        m_playerAdjust = p.torch_player_adjust;
        m_subsystemOffset = p.torch_subsystem_offset;
        m_componentOffset = p.torch_component_offset;
        m_componentGuard = p.torch_component_guard;
    }
    // Says plainly that nothing is gated. A player reading this log after "head
    // tracking does nothing in my save" needs to know in one line that the game
    // state is not what is stopping them, because for two sessions it was.
    Log::Line("Game state: build %s recognised; head tracking is suppressed in the main menu "
              "and while paused. The menu is where the player has no equipment component - the "
              "engine's own level flags are logged beside it but NOT used, because they read "
              "\"no level\" in a tunnel during normal gameplay",
              p.name);
}

// Walks to the player's equipment component. Null whenever any step is
// unreadable, which is the ordinary state at the main menu and during a load.
bool GameState::PlayerComponentExists() const {
    if (m_playerRoot == 0) return true;   // not derived on this build: never suppress

    uintptr_t g = 0;
    if (!cameraunlock::memory::SafeRead(m_playerRoot, g) || g == 0) return false;
    uintptr_t first = 0;
    if (!cameraunlock::memory::SafeRead(g + 0x28, first) || first == 0) return false;
    uintptr_t holder = 0;
    if (!cameraunlock::memory::SafeRead(first + 0xE0, holder) || holder == 0) return false;
    uintptr_t interior = 0;
    if (!cameraunlock::memory::SafeRead(holder, interior) || interior == 0) return false;

    const uintptr_t player = interior - m_playerAdjust;
    uintptr_t sub = 0;
    if (!cameraunlock::memory::SafeRead(player + m_subsystemOffset, sub) || sub == 0) return false;

    // The engine's own component fetch bails on this short, and at the main menu
    // it is zero while a level has it at 32.
    uint16_t guard = 0;
    if (!cameraunlock::memory::SafeRead(sub + m_componentGuard, guard) || guard == 0) return false;

    uintptr_t component = 0;
    if (!cameraunlock::memory::SafeRead(sub + m_componentOffset, component)) return false;
    return component != 0;
}

void GameState::Update() {
    if (m_root == 0) return;

    if (++m_framesSincePoll < kPollFrames) return;
    m_framesSincePoll = 0;

    // NOTHING BELOW MAY LEAVE THE PREVIOUS VERDICT STANDING. Every step here can
    // fail - a pointer the engine has not written yet, one it has just freed, a
    // mode whose class does not carry the byte - and the version that shipped
    // answered all of them with a bare `return`, which holds whatever the last
    // successful read said. Hold the wrong way once and it never comes back: a
    // player who walked out of the main menu into a level, on a frame where the
    // chain then stopped being walkable, kept the menu's verdict for the whole
    // level with nothing in the log to say so. That is the "loaded straight into
    // the game and head tracking is dead" report.
    //
    // So only a POSITIVE reading of a known no-level value, off the one class
    // that carries it, suppresses. Everything else - unreadable, absent, foreign
    // class, a byte outside the known set - is UNKNOWN, and unknown leaves head
    // tracking on. The cost of the wrong answer that way round is a view that
    // drifts under a menu; the cost the other way round is the mod appearing not
    // to work at all.
    uintptr_t object = 0;
    if (!cameraunlock::memory::SafeRead(m_root, object) || object == 0) {
        Unknown(Chain::NoGameObject, 0);
        return;
    }

    uintptr_t inner = 0;
    if (!cameraunlock::memory::SafeRead(object + m_objectOffset, inner) || inner == 0) {
        Unknown(Chain::NoModeObject, 0);
        return;
    }

    // The engine's own guard. The mode object is polymorphic and the byte is on
    // one class only: the engine compares the vtable against one exact address
    // and reads the byte on a match, asking the virtual at vtable+0x1C0
    // otherwise. Read unguarded, the byte is whatever the OTHER class keeps at
    // that offset, and a foreign zero there is indistinguishable from the main
    // menu. The mod does not make the virtual call.
    uintptr_t vtable = 0;
    if (!cameraunlock::memory::SafeRead(inner, vtable)) {
        Unknown(Chain::NoVtable, 0);
        return;
    }
    if (vtable != m_vtable) {
        Unknown(Chain::ForeignMode, vtable - m_base);
        return;
    }

    uint8_t state = 0;
    if (!cameraunlock::memory::SafeReadU8(inner + m_flagOffset, state)) {
        Unknown(Chain::NoStateByte, 0);
        return;
    }

    // Read from the same object, under the same vtable guard, so the two cannot
    // disagree about which class they were read from.
    uint8_t pause = kNotPaused;
    if (m_pauseOffset != 0 && !cameraunlock::memory::SafeReadU8(inner + m_pauseOffset, pause)) {
        Unknown(Chain::NoStateByte, 0);
        return;
    }

    const Level level = ReadLevel(state);
    const Pause paused = ReadPause(pause);
    // ONLY THE PAUSE BYTE GATES. The level flags are read and logged, and nothing
    // acts on them, because +0x2B reads "no level" - 0xC2, the same value the main
    // menu gives - while the player stands in a tunnel early in the game. Measured
    // from a player's log: the flag followed them in and out of it three times and
    // held 0xC2 for 37 seconds inside.
    //
    // Second field on this game to look like a level flag and turn out to be
    // something narrower, and the lesson is +0x6BC's: a suppressor that is wrong
    // during gameplay costs the whole mod, while no suppressor costs a view that
    // drifts under a menu. Do not re-gate on this until a candidate has been
    // watched through a STREAMING BOUNDARY as well as a menu - that tunnel is the
    // test case.
    // The MAIN MENU and only the main menu: the engine says no level AND it is
    // rendering the menu scene's own aspect. Either alone is wrong - the flags
    // say "no level" in a tunnel, and a player whose display really is 3:2 would
    // match the aspect in gameplay.
    // The menu is where the player has no equipment component. The engine's own
    // level flags are read and logged beside it, and deliberately not used.
    (void)level;
    const bool haveComponent = PlayerComponentExists();
    m_inGameplay = haveComponent && paused != Pause::Yes;
    m_lastComponent = haveComponent;
    Report(Chain::Read, state, pause);
}

// Head tracking stays on, and the reason is logged the first time each distinct
// one appears. Edge-triggered on the pair, so a mode that alternates with a
// readable one does not write a line every poll.
void GameState::Unknown(Chain chain, uintptr_t detail) {
    m_inGameplay = true;
    if (chain == Chain::ForeignMode && detail != m_lastForeignVtable) {
        m_lastForeignVtable = detail;
        m_lastChain = Chain::None;
    }
    Report(chain, 0, 0);
}

void GameState::Report(Chain chain, uint8_t state, uint8_t pause) {
    if (chain == m_lastChain && state == m_lastState && pause == m_lastPause) return;
    m_lastChain = chain;
    m_lastState = state;
    m_lastPause = pause;

    switch (chain) {
        case Chain::Read:
            // The byte itself is in the line. Without it a report of head
            // tracking switching itself off says only "an unrecognised state
            // byte", which is the one case where the VALUE is the whole
            // diagnostic and the only way to learn what else this field holds.
            if (!m_inGameplay && ReadPause(pause) != Pause::Yes) {
                Log::Line("Game state: main menu - head tracking suppressed (no player "
                          "equipment; engine flags 0x%02X)",
                          state);
                return;
            }
            if (ReadPause(pause) == Pause::Yes && ReadLevel(state) == Level::Up) {
                Log::Line("Game state: paused - head tracking suppressed (flags 0x%02X, pause %u)",
                          state, pause);
                return;
            }
            Log::Line("Game state: %s (flags 0x%02X, pause %u)", StateName(state), state, pause);
            return;
        case Chain::NoGameObject:
            Log::Line("Game state: the engine has not built its game object yet");
            return;
        case Chain::NoModeObject:
            Log::Line("Game state: the engine is between game modes - nothing to read the state off");
            return;
        case Chain::NoVtable:
            Log::Line("Game state: the game mode object could not be read");
            return;
        case Chain::ForeignMode:
            Log::Line("Game state: the engine is running a game mode this mod does not know how "
                      "to read (vtable +0x%llX)",
                      static_cast<unsigned long long>(m_lastForeignVtable));
            return;
        case Chain::NoStateByte:
            Log::Line("Game state: the state byte could not be read");
            return;
        case Chain::None:
            return;
    }
}

}  // namespace metroex
