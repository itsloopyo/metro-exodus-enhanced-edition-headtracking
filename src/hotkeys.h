#pragma once

#include "config.h"

#include "cameraunlock/input/hotkey_poller.h"

#include <functional>
#include <string>

namespace metroex {

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
    void Register(const std::string& keys, const char* setting, Action action);

    cameraunlock::input::HotkeyPoller m_poller;
    bool m_started = false;
};

}
