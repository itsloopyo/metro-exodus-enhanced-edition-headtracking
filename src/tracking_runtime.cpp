#include "tracking_runtime.h"

#include "logging.h"

#include "cameraunlock/math/smoothing_utils.h"

namespace metroex {

namespace {

const char* ModeName(cameraunlock::TrackingMode mode) {
    switch (mode) {
        case cameraunlock::TrackingMode::RotationOnly:
            return "rotation only";
        case cameraunlock::TrackingMode::PositionOnly:
            return "position only";
        case cameraunlock::TrackingMode::RotationAndPosition:
            break;
    }
    return "rotation and position";
}

// The cycle HeadTrackingSession::CycleMode walks: rotation and position, rotation
// only, position only, and round again.
cameraunlock::TrackingMode NextMode(cameraunlock::TrackingMode mode) {
    switch (mode) {
        case cameraunlock::TrackingMode::RotationAndPosition:
            return cameraunlock::TrackingMode::RotationOnly;
        case cameraunlock::TrackingMode::RotationOnly:
            return cameraunlock::TrackingMode::PositionOnly;
        case cameraunlock::TrackingMode::PositionOnly:
            break;
    }
    return cameraunlock::TrackingMode::RotationAndPosition;
}

const char* YawAxisName(bool worldSpaceYaw) {
    return worldSpaceYaw ? "world up-axis (horizon locked)" : "camera up-axis";
}

}  // namespace

cameraunlock::TrackingMode StartupTrackingMode(const Config& cfg) {
    // The config table reads a pair that names no mode as its defaults, so the
    // pair always decodes.
    return cameraunlock::DecodeTrackingMode(cfg.rotation_enabled, cfg.position_enabled).value();
}

void TrackingRuntime::Start(const Config& cfg) {
    m_cfg = cfg;

    // Both values go to the session; which one applies is decided per frame from
    // the packet source address, which Update() reads off the receiver itself.
    m_session.SetLocalSmoothing(m_cfg.local_smoothing);
    m_session.SetRemoteSmoothing(m_cfg.remote_smoothing);

    // SetPositionSettings, not GetPositionProcessor().SetSettings(): the session
    // owns the two smoothing values and recomposes them onto the struct.
    m_session.SetPositionSettings(m_cfg.position);

    const cameraunlock::TrackingMode mode = StartupTrackingMode(m_cfg);
    m_session.SetMode(mode);
    m_desiredMode.store(static_cast<int>(mode), std::memory_order_relaxed);
    m_appliedMode.store(static_cast<int>(mode), std::memory_order_release);
    Log::Line("Tracking mode: %s", ModeName(mode));

    m_receiver.SetLog([](const std::string& msg) { Log::Line("UDP: %s", msg.c_str()); });

    // Reported either way. "Did the tracker link come up" is the first question
    // a bug report has to answer, and a silent success leaves it answerable only
    // by the absence of a warning.
    if (m_receiver.Start(static_cast<uint16_t>(m_cfg.udp_port))) {
        Log::Line("Tracker: listening on UDP port %d", m_cfg.udp_port);
    } else {
        Log::Line("WARN: UDP receiver did not bind immediately on port %d; background retry active",
                  m_cfg.udp_port);
    }

    m_worldSpaceYaw.store(m_cfg.world_space_yaw, std::memory_order_relaxed);
    Log::Line("Yaw axis: %s", YawAxisName(m_cfg.world_space_yaw));

    m_enabled.store(m_cfg.enable_on_startup, std::memory_order_release);
}

void TrackingRuntime::Stop() { m_receiver.Stop(); }

void TrackingRuntime::ToggleEnabled() {
    bool prev = m_enabled.load(std::memory_order_relaxed);
    m_enabled.store(!prev, std::memory_order_relaxed);
    Log::Line("Tracking %s", !prev ? "enabled" : "disabled");
}

bool TrackingRuntime::ToggleYawMode() {
    const bool next = !m_worldSpaceYaw.load(std::memory_order_relaxed);
    m_worldSpaceYaw.store(next, std::memory_order_relaxed);
    Log::Line("Yaw axis: %s", YawAxisName(next));
    return next;
}

cameraunlock::TrackingMode TrackingRuntime::CycleMode() {
    // Applied on the render thread, so a cycle cannot land between the rotation
    // and position reads of one frame. Acknowledged here rather than there,
    // because on a build the camera hook could not land on there is no render
    // frame to apply it in and a key that logged nothing would read as a key
    // that does nothing. The wording promises only that the key was seen: the
    // line saying the mode applied comes from the frame that applies it, and on
    // such a build it never comes.
    const cameraunlock::TrackingMode next = NextMode(
        static_cast<cameraunlock::TrackingMode>(m_appliedMode.load(std::memory_order_acquire)));
    m_desiredMode.store(static_cast<int>(next), std::memory_order_relaxed);
    m_modeRequested.store(true, std::memory_order_release);
    Log::Line("Tracking mode: %s requested; it applies on the next frame the mod draws",
              ModeName(next));
    return next;
}

TrackingState TrackingRuntime::SamplePerFrame(bool inGameplay, bool aiming, HeadPose& out) {
    out = HeadPose{};

    // Ticked unconditionally, so the transition keeps its own clock while the
    // pose is suppressed and the first frame back gets a real dt rather than the
    // whole suppressed stretch clamped to the ceiling.
    const float dt = m_clock.Tick();
    m_adsClockMs += static_cast<double>(dt) * 1000.0;
    const unsigned long long nowMs = static_cast<unsigned long long>(m_adsClockMs);

    // The mode change is requested from the hotkey thread and applied here, on
    // the render thread, so a cycle can never land between the rotation and
    // position reads below and produce half of one mode and half of the next.
    if (m_modeRequested.exchange(false, std::memory_order_acq_rel)) {
        const auto desired =
            static_cast<cameraunlock::TrackingMode>(m_desiredMode.load(std::memory_order_relaxed));
        m_session.SetMode(desired);
        m_appliedMode.store(static_cast<int>(desired), std::memory_order_release);
        Log::Line("Tracking mode: %s", ModeName(desired));
    }

    const bool enabled = m_enabled.load(std::memory_order_acquire);
    const bool receiving = m_receiver.IsReceiving();

    // Ahead of the gate below, and off the receiver rather than off the verdict:
    // the tracker going quiet is the one thing here the player cannot see the
    // cause of, and it has to be in the log whether tracking was switched off or
    // the game was in a menu when it happened.
    LogTrackerPresence(receiving);

    // The gate is NOT `receiving`. A tracker that goes quiet - a webcam that
    // loses the face, a phone that drops off the WiFi - must leave the view where
    // the head last was, not snap it back to the game's camera and snap it out
    // again when the face is re-acquired. The session's own Update() answers
    // false until the first packet ever arrives and holds the last pose after
    // that, which is exactly the "hold, never snap" the doctrine asks for.
    HeadPose absolute;
    bool live = false;
    if (enabled && m_session.Update(dt)) {
        // Update() re-reads the receiver connection locality every frame, so
        // switching between a local OpenTrack instance and a phone on WiFi picks
        // up the other smoothing parameter without a restart. Log the transitions.
        LogConnectionLocality();
        if (m_session.GetRotation(absolute.yaw, absolute.pitch, absolute.roll)) {
            live = true;
            absolute.has_position =
                m_session.GetPositionOffset(absolute.x, absolute.y, absolute.z);
        }
    }

    const TrackingState state = DecideTracking(enabled, inGameplay, live, aiming);

    if (!PoseApplies(state.verdict)) {
        // Only when the sights are down. Resetting while they are up puts the
        // fade back at the hip, and the frame the suppression lifts on would
        // then hand the camera the whole lean for one frame before easing it
        // back out.
        if (!aiming) m_adsFade.Reset();
        return state;
    }

    out = EaseLeanForSights(absolute, m_adsFade.Update(state.aiming, nowMs));
    return state;
}

void TrackingRuntime::LogTrackerPresence(bool receiving) {
    if (receiving == m_receiving) {
        return;
    }
    m_receiving = receiving;
    if (receiving) {
        Log::Line("Tracker: pose data is arriving on UDP port %u", m_cfg.udp_port);
    } else {
        Log::Line("Tracker: pose data stopped arriving on UDP port %u; the view holds the last "
                  "pose it was sent until it resumes",
                  m_cfg.udp_port);
    }
}

void TrackingRuntime::LogConnectionLocality() {
    const bool isRemote = m_session.IsRemoteConnection();
    if (m_localityKnown && isRemote == m_isRemoteConnection) {
        return;
    }
    m_isRemoteConnection = isRemote;
    m_localityKnown = true;
    Log::Line("Tracker connection is %s; smoothing=%.2f", isRemote ? "remote" : "local",
              cameraunlock::math::GetEffectiveSmoothing(m_cfg.local_smoothing,
                                                        m_cfg.remote_smoothing, isRemote));
}

}  // namespace metroex
