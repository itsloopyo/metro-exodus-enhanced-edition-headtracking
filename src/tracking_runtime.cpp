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

const char* YawAxisName(bool worldSpaceYaw) {
    return worldSpaceYaw ? "world up-axis (horizon locked)" : "camera up-axis";
}

}  // namespace

void TrackingRuntime::Start(const Config& cfg) {
    m_cfg = cfg;

    cameraunlock::SensitivitySettings sens;
    sens.yaw = m_cfg.sens_yaw;
    sens.pitch = m_cfg.sens_pitch;
    sens.roll = m_cfg.sens_roll;
    sens.invert_yaw = m_cfg.invert_yaw;
    sens.invert_pitch = m_cfg.invert_pitch;
    sens.invert_roll = m_cfg.invert_roll;
    m_session.GetProcessor().SetSensitivity(sens);

    // Both values go to the session; which one applies is decided per frame from
    // the packet source address, which Update() reads off the receiver itself.
    m_session.SetLocalSmoothing(m_cfg.local_smoothing);
    m_session.SetRemoteSmoothing(m_cfg.remote_smoothing);

    cameraunlock::PositionSettings pos;
    pos.sensitivity_x = m_cfg.pos_sens_x;
    pos.sensitivity_y = m_cfg.pos_sens_y;
    pos.sensitivity_z = m_cfg.pos_sens_z;
    pos.limit_x = m_cfg.pos_limit_x;
    pos.limit_y = m_cfg.pos_limit_y;
    pos.limit_y_down = m_cfg.pos_limit_y_down;
    pos.limit_z = m_cfg.pos_limit_z;
    pos.limit_z_back = m_cfg.pos_limit_z_back;
    // SetPositionSettings, not GetPositionProcessor().SetSettings(): the session
    // owns the two smoothing values and recomposes them onto the struct.
    m_session.SetPositionSettings(pos);

    m_session.SetMode(m_cfg.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                                             : cameraunlock::TrackingMode::RotationOnly);

    m_receiver.SetLog([](const std::string& msg) { Log::Line("UDP: %s", msg.c_str()); });

    // Reported either way. "Did the tracker link come up" is the first question
    // a bug report has to answer, and a silent success leaves it answerable only
    // by the absence of a warning.
    if (m_receiver.Start(m_cfg.udp_port)) {
        Log::Line("Tracker: listening on UDP port %u", m_cfg.udp_port);
    } else {
        Log::Line("WARN: UDP receiver did not bind immediately on port %u; background retry active",
                  m_cfg.udp_port);
    }

    m_worldSpaceYaw.store(m_cfg.world_space_yaw, std::memory_order_relaxed);
    Log::Line("Yaw axis: %s", YawAxisName(m_cfg.world_space_yaw));

    m_adsMode.store(static_cast<int>(m_cfg.ads_mode), std::memory_order_release);
    Log::Line("[ads] %s", AdsModeToast(m_cfg.ads_mode));

    m_enabled.store(m_cfg.enabled_on_startup, std::memory_order_release);
}

void TrackingRuntime::Stop() { m_receiver.Stop(); }

void TrackingRuntime::ToggleEnabled() {
    bool prev = m_enabled.load(std::memory_order_relaxed);
    m_enabled.store(!prev, std::memory_order_relaxed);
    Log::Line("Tracking %s", !prev ? "enabled" : "disabled");
}

void TrackingRuntime::ToggleYawMode() {
    const bool next = !m_worldSpaceYaw.load(std::memory_order_relaxed);
    m_worldSpaceYaw.store(next, std::memory_order_relaxed);
    Log::Line("Yaw axis: %s", YawAxisName(next));
}

void TrackingRuntime::CycleMode() {
    // Applied on the render thread, so a cycle cannot land between the rotation
    // and position reads of one frame. Acknowledged here rather than there,
    // because on a build the camera hook could not land on there is no render
    // frame to apply it in and a key that logged nothing would read as a key
    // that does nothing. The wording promises only that the key was seen: the
    // line naming the new mode comes from the frame that applies it, and on such
    // a build it never comes.
    m_modeCycleRequested.store(true, std::memory_order_release);
    Log::Line("Tracking mode: cycle requested; it applies on the next frame the mod draws");
}

void TrackingRuntime::CycleAdsMode() {
    const AdsMode next = NextAdsMode(GetAdsMode());
    m_adsMode.store(static_cast<int>(next), std::memory_order_release);
    m_cfg.SaveAdsMode(next);
    // The camera hook reads this atomic at the top of the very next frame and
    // hands it to both SamplePerFrame() and the aim mark, and the verdict is
    // decided from scratch there, so a change made mid-aim lands on that aim
    // rather than on the next one.
    Log::Line("[ads] %s", AdsModeToast(next));
}

TrackingState TrackingRuntime::SamplePerFrame(bool inGameplay, bool aiming, AdsMode adsMode,
                                              HeadPose& out) {
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
    if (m_modeCycleRequested.exchange(false, std::memory_order_acq_rel)) {
        Log::Line("Tracking mode: %s", ModeName(m_session.CycleMode()));
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
    AdsEntryPose::Pose absolute;
    bool live = false;
    bool havePosition = false;
    if (enabled && m_session.Update(dt)) {
        // Update() re-reads the receiver connection locality every frame, so
        // switching between a local OpenTrack instance and a phone on WiFi picks
        // up the other smoothing parameter without a restart. Log the transitions.
        LogConnectionLocality();
        if (m_session.GetRotation(absolute.yaw, absolute.pitch, absolute.roll)) {
            live = true;
            havePosition = m_session.GetPositionOffset(absolute.x, absolute.y, absolute.z);
        }
    }

    const TrackingState state = DecideTracking(enabled, inGameplay, live, aiming, adsMode);

    if (!PoseApplies(state.verdict)) {
        // The entry pose goes on every suppressed frame, so an aim that resumes
        // re-captures where the head is now rather than resuming against a pose
        // from before the menu.
        m_adsEntry.Reset();
        // The TRANSITION only goes when the sights are down. Resetting it while
        // they are up puts the fade back at the hip, and the frame the
        // suppression lifts on then hands the camera the player's whole head
        // angle for one frame before easing back onto the gun - the jolt the
        // fade exists to remove, delivered by its caller.
        if (!aiming) m_adsFade.Reset();
        return state;
    }

    // `aiming` is the game's own state, never the verdict: in `paused` the gate
    // reports AdsSuspended precisely because the sights are up, so feeding that
    // back would make the fade chase itself.
    const float scale = m_adsFade.Update(aiming, nowMs);
    const AdsEntryPose::Pose relative = m_adsEntry.Relative(aiming, live, absolute);
    const AdsEntryPose::Pose blended = BlendAdsPose(adsMode, scale, absolute, relative);

    // Re-clamped, because the relative pose is a DIFFERENCE of two already
    // clamped poses and so reaches twice the limit: lean 0.30m forward, raise the
    // sights, lean 0.10m back and the relative z is +0.40, four times the 0.10
    // that stops the eye backing through the player model.
    const cameraunlock::math::Vec3 bounded =
        m_session.GetPositionProcessor().ClampToLimits(
            cameraunlock::math::Vec3(blended.x, blended.y, blended.z));

    out.yaw = blended.yaw;
    out.pitch = blended.pitch;
    out.roll = blended.roll;
    out.x = bounded.x;
    out.y = bounded.y;
    out.z = bounded.z;
    out.has_position = havePosition;
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
