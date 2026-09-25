#include "hotkeys.h"

#include "logging.h"

#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace metroex {

namespace {

// How often the poller reads the keyboard. ~60Hz is the fleet-wide value: fast
// enough that a tap between two polls is not lost, slow enough to cost nothing.
constexpr int kPollIntervalMs = 16;

}  // namespace

void Hotkeys::Register(const std::string& keys, const char* setting, Action action) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(keys);
    if (!parsed.ok()) {
        // The config table read the list with the same parser, so this is a bug.
        throw std::logic_error(std::string(setting) + "=" + keys + " is not a key list: " + parsed.error);
    }
    // A plain key does not fire while Ctrl and Shift are both held, so Ctrl+Shift
    // with that key reaches only a binding that names the chord.
    cameraunlock::input::RegisterKeyBindings(m_poller, parsed.bindings, std::move(action));
}

bool Hotkeys::Start(const Config& cfg, Actions actions) {
    if (m_started) return true;

    Register(cfg.toggle_key_name, "ToggleKey", std::move(actions.toggle));
    Register(cfg.cycle_tracking_mode_key_name, "CycleTrackingModeKey", std::move(actions.cycleMode));
    Register(cfg.yaw_mode_key_name, "YawModeKey", std::move(actions.yawMode));

    if (!m_poller.Start(kPollIntervalMs)) {
        Log::Line("ERROR: HotkeyPoller failed to start");
        return false;
    }

    Log::Line("Hotkeys: ToggleKey=%s, CycleTrackingModeKey=%s, YawModeKey=%s",
              cfg.toggle_key_name.c_str(), cfg.cycle_tracking_mode_key_name.c_str(),
              cfg.yaw_mode_key_name.c_str());

    m_started = true;
    return true;
}

}  // namespace metroex
