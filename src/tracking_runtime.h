#pragma once

#include "ads.h"
#include "ads_gate.h"
#include "config.h"
#include "head_transform.h"

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

#include <atomic>

namespace metroex {

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
    // reach the camera; the verdict also carries whether the sights are up, which
    // is reported in every ADS mode including `paused`.
    //
    // The verdict is recomputed here from scratch every frame, so cycling the ADS
    // mode mid-aim takes effect on that aim rather than the next one.
    //
    // `adsMode` is passed in rather than read from the atomic here because the
    // caller needs the same value for the aim mark. Read twice, a press of the
    // cycle key landing between the two reads would give one frame whose pose is
    // in one mode and whose mark is in the next.
    TrackingState SamplePerFrame(bool inGameplay, bool aiming, AdsMode adsMode, HeadPose& out);

    void ToggleEnabled();
    void CycleMode();

    // Flips the yaw axis between the world up-axis and the camera's own. Called
    // from the hotkey thread; the camera hook reads IsWorldSpaceYaw() per frame,
    // so the switch lands on the very next frame and needs no restart. The
    // choice is not written back to the INI: the config key is the mode the mod
    // starts on, and a session change is a session change.
    void ToggleYawMode();

    // Advances the ADS cycle, saves it, and logs the toast for the mode it
    // switched to. Called from the hotkey thread.
    void CycleAdsMode();

    AdsMode GetAdsMode() const {
        return static_cast<AdsMode>(m_adsMode.load(std::memory_order_acquire));
    }

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
    std::atomic<bool> m_modeCycleRequested{false};

    // Written from the hotkey thread, read by the camera hook on the render
    // thread. Initialised from the config in Start().
    std::atomic<bool> m_worldSpaceYaw{true};

    // Held as an int because AdsMode is an enum class and std::atomic over one
    // is not lock-free on every toolchain. Written from the hotkey thread, read
    // once per frame on the render thread.
    std::atomic<int> m_adsMode{static_cast<int>(kDefaultAdsMode)};

    // Render-thread only. The fade owns the shape of the transition into and out
    // of the aim; the entry pose is what makes the tracked modes carry on from
    // where the sights came up rather than from centre.
    AdsFade m_adsFade;
    AdsEntryPose m_adsEntry;

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
