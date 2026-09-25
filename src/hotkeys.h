#pragma once

#include "config.h"

#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/key_bindings.h"

#include <functional>
#include <vector>

namespace metroex {

struct HotkeyBindings {
    std::vector<cameraunlock::input::KeyBinding> toggle;
    std::vector<cameraunlock::input::KeyBinding> cycleMode;
    std::vector<cameraunlock::input::KeyBinding> yawMode;
};

// The three key lists of the settings file, parsed. Throws std::logic_error for a list that
// does not parse: the config table read each one with the same parser, so that is a bug.
HotkeyBindings ParseHotkeys(const Config& cfg);

class Hotkeys {
public:
    using Action = std::function<void()>;

    struct Actions {
        Action toggle;
        Action cycleMode;
        Action yawMode;
    };

    // Registers the three key lists of the settings file on the hotkey thread.
    bool Start(const Config& cfg, Actions actions);

private:
    cameraunlock::input::HotkeyPoller m_poller;
    bool m_started = false;
};

}
