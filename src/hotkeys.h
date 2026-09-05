#pragma once

#include "config.h"

#include "cameraunlock/input/hotkey_poller.h"

#include <functional>

namespace metroex {

class Hotkeys {
public:
    using Action = std::function<void()>;

    struct Actions {
        Action toggle;
        Action cycleMode;
        Action yawMode;
        Action adsMode;
    };

    bool Start(const Config& cfg, Actions actions);

private:
    cameraunlock::input::HotkeyPoller m_poller;
    bool m_started = false;
};

}
