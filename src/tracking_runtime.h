#pragma once

#include "ads.h"
#include "ads_gate.h"
#include "config.h"
#include "head_transform.h"

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <atomic>

namespace metroex {

cameraunlock::TrackingMode StartupTrackingMode(const Config& cfg);

class TrackingRuntime {
public:
    TrackingRuntime() : m_session(m_receiver) {}

    // The tracker link is the one thing here that is allowed to come up late: a
    // port held by a previously-launched game frees itself, and the receiver
    // retries in the background. So there is no failure for a caller to act on,
    // and both outcomes are reported in the log rather than returned.
    void Start(const Config& cfg);
    void Stop();

    // Called once per rendered frame. `inGameplay` and `aiming` are this frame's
    // game state, both polled by the caller rather than latched. Fills `out` and
    // returns a verdict for which PoseApplies() is true whenever the pose should
    // reach the camera. With the sights up the rotation is passed through
    // untouched and only the lean eases out - see ads.h.
    TrackingState SamplePerFrame(bool inGameplay, bool aiming, HeadPose& out);

    void ToggleEnabled();

    // Asks for the tracking mode after the one the render thread last applied, and
    // returns it. Called from the hotkey thread; the render thread applies it on its
    // next frame. Stepping from the applied mode rather than from the last one asked
    // for keeps two presses before one frame to one step, so the mode the caller
    // saves is the mode the frame applies.
    cameraunlock::TrackingMode CycleMode();

    // Flips the yaw axis between the world up-axis and the camera's own, and
    // returns the new setting (true: world up-axis). Called from the hotkey
    // thread; the camera hook reads IsWorldSpaceYaw() per frame, so the switch
    // lands on the very next frame and needs no restart.
    bool ToggleYawMode();

    bool IsEnabled() const { return m_enabled.load(std::memory_order_relaxed); }

    // True while head yaw turns about the world up-axis (horizon locked), false
    // while it turns about the camera's own up-axis.
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(std::memory_order_relaxed); }

private:
    using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;

    // Without this the session would silently report every tracker as local and
    // pin smoothing to local_smoothing forever, with no compile error.
    static_assert(Session::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() for per-connection smoothing");

    // Reports transitions between a local and a remote tracker. The session
    // itself does the per-frame selection.
    void LogConnectionLocality();

    // Reports the tracker link coming up and going away. `receiving` is read off
    // the receiver rather than off the verdict, so switching tracking off or
    // walking into a menu is not reported as the tracker having gone: those have
    // their own lines, and a bug report that says "it stopped halfway through"
    // needs to separate them. Passed in rather than read here because
    // IsReceiving() reads a clock - two calls cost two reads and can disagree
    // within one frame.
    void LogTrackerPresence(bool receiving);

    // Frame dt is clamped to this ceiling so a stall (alt-tab, load hitch) cannot
    // feed a huge dt into the smoothing/extrapolation math.
    static constexpr float kMaxFrameDtSec = 0.25f;

    Config m_cfg{};
    cameraunlock::UdpReceiver m_receiver;
    Session m_session;
    cameraunlock::time::FrameClock m_clock{kMaxFrameDtSec};

    std::atomic<bool> m_enabled{false};

    // The mode the hotkey thread asked for and the one the render thread last
    // applied, as TrackingMode values. Both start on the configured mode.
    std::atomic<int> m_desiredMode{0};
    std::atomic<int> m_appliedMode{0};
    std::atomic<bool> m_modeRequested{false};

    // Written from the hotkey thread, read by the camera hook on the render
    // thread. Initialised from the config in Start().
    std::atomic<bool> m_worldSpaceYaw{true};

    // Render-thread only. Owns the shape of the lean easing out as the sights
    // come up and back in as they go down.
    AdsFade m_adsFade;

    // Milliseconds accumulated from the frame clock rather than GetTickCount64,
    // whose ~16ms granularity would quantise a 150ms transition into ten steps.
    double m_adsClockMs = 0.0;

    // Render-thread only: last known connection locality, for change logging.
    bool m_isRemoteConnection = false;
    bool m_localityKnown = false;

    // Render-thread only: last known tracker presence, for change logging.
    // Seeded false because that is the true state before the first packet, which
    // is what keeps the launch from opening with a "data stopped arriving" line
    // for a tracker that has not started yet.
    bool m_receiving = false;
};

}
