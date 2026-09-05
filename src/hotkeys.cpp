#include "hotkeys.h"

#include "logging.h"

#include "cameraunlock/input/chord_hotkeys.h"

namespace metroex {

namespace {

// How often the poller reads the keyboard. ~60Hz is the fleet-wide value: fast
// enough that a tap between two polls is not lost, slow enough to cost nothing.
constexpr int kPollIntervalMs = 16;

}  // namespace

bool Hotkeys::Start(const Config& cfg, Actions actions) {
    if (m_started) return true;

    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;

    m_poller.SetToggleKey(cfg.vk_toggle, NavGuarded(actions.toggle));
    m_poller.AddHotkey(cfg.vk_cycle_mode, NavGuarded(actions.cycleMode));
    m_poller.AddHotkey(cfg.vk_yaw_mode, NavGuarded(actions.yawMode));
    m_poller.AddHotkey(cfg.vk_ads_mode, NavGuarded(actions.adsMode));

    // Chord alternatives on the same poller for keyboards without a nav cluster.
    // ChordGuarded gates each action on the modifier state; NavGuarded above
    // keeps the nav keys from firing as well while the chord is held.
    if (cfg.chord_toggle) m_poller.AddHotkey('Y', ChordGuarded(actions.toggle));
    if (cfg.chord_cycle_mode) m_poller.AddHotkey('G', ChordGuarded(actions.cycleMode));
    if (cfg.chord_yaw_mode) m_poller.AddHotkey('H', ChordGuarded(actions.yawMode));
    if (cfg.chord_ads_mode) m_poller.AddHotkey('U', ChordGuarded(actions.adsMode));

    if (!m_poller.Start(kPollIntervalMs)) {
        Log::Line("ERROR: HotkeyPoller failed to start");
        return false;
    }

    Log::Line("Hotkeys: toggle=0x%02X cycleMode=0x%02X yawMode=0x%02X adsMode=0x%02X "
              "chords(Y/G/H/U)=%d/%d/%d/%d",
              cfg.vk_toggle, cfg.vk_cycle_mode, cfg.vk_yaw_mode, cfg.vk_ads_mode,
              cfg.chord_toggle ? 1 : 0, cfg.chord_cycle_mode ? 1 : 0,
              cfg.chord_yaw_mode ? 1 : 0, cfg.chord_ads_mode ? 1 : 0);

    m_started = true;
    return true;
}


}  // namespace metroex
