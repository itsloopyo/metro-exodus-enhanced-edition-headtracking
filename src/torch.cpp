#include "torch.h"

#include "build_profile.h"
#include "logging.h"

#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/memory/safe_memory.h"

#include <cmath>

namespace metroex {

namespace {

// The pose the beam should be turned by, published by the camera hook each frame
// and consumed by the detour on whatever thread runs the light update. Plain
// floats: a torn read costs one frame of a slightly wrong beam angle, and a lock
// on the engine's own render path costs far more than that.
struct BeamTurn {
    volatile float right[3];
    volatile float up[3];
    volatile float forward[3];
    volatile uintptr_t destination;   // the float basis this applies to
    volatile bool valid;
};
BeamTurn g_turn{};

using NarrowFn = void*(__fastcall*)(void*, void*);
NarrowFn g_narrowOriginal = nullptr;

// The engine's narrowing routine, hooked. It runs first - so the basis it writes
// is the engine's - and then the beam is turned in place. Nothing writes it again
// this frame, which is the whole point: writing before this ran is what made
// every earlier attempt do nothing.
void* __fastcall NarrowDetour(void* dst, void* src) {
    void* result = g_narrowOriginal(dst, src);
    if (g_turn.valid && reinterpret_cast<uintptr_t>(dst) == g_turn.destination) {
        float* f = static_cast<float*>(dst);
        // Layout from the disassembly: +0x00 right, +0x10 up, +0x20 forward.
        f[0] = g_turn.right[0];   f[1] = g_turn.right[1];   f[2] = g_turn.right[2];
        f[4] = g_turn.up[0];      f[5] = g_turn.up[1];      f[6] = g_turn.up[2];
        f[8] = g_turn.forward[0]; f[9] = g_turn.forward[1]; f[10] = g_turn.forward[2];
    }
    return result;
}

bool ReadPtr(uintptr_t at, uintptr_t& out) {
    return cameraunlock::memory::SafeRead(at, out) && out != 0;
}

// A degenerate axis reaches the engine as a beam pointing nowhere. The camera
// hook has the same guard for the view; this one is local because a light and a
// camera failing the same way still fail independently.
bool IsUsableAxis(const Vec3& v) {
    const float m = v.SqrMagnitude();
    return std::isfinite(m) && m > 1e-6f;
}

}  // namespace

void Torch::Initialise(bool followsHead, float multiplier, bool watchWrites) {
    if (!followsHead) {
        Log::Line("Torch: LightFollowsHead is off; the beam stays on the aim");
        return;
    }
    if (!(multiplier >= 0.0f && multiplier <= cameraunlock::effects::kMaxLightMultiplier)) {
        Log::Line("Torch: LightMultiplier %.2f is outside 0 to %.1f; the beam stays on the aim",
                  multiplier, cameraunlock::effects::kMaxLightMultiplier);
        return;
    }

    const ResolvedBuild build = ResolveRunningBuild();
    if (const char* cause = BuildLookupCause(build.outcome)) {
        Log::Line("Torch: %s; the beam stays on the aim", cause);
        return;
    }
    const BuildProfile& p = *build.profile;
    if (p.torch_root_rva == 0) {
        Log::Line("Torch: build %s is recognised but its torch addresses have not been "
                  "derived; the beam stays on the aim",
                  p.name);
        return;
    }
    if (!RvaFits(p.torch_root_rva, sizeof(void*), build.fingerprint.SizeOfImage)) {
        Log::Line("ERROR: build profile %s puts the torch outside the image; the beam stays "
                  "on the aim",
                  p.name);
        return;
    }

    m_base = reinterpret_cast<uintptr_t>(build.base);
    m_rootRva = p.torch_root_rva;
    m_playerAdjust = p.torch_player_adjust;
    m_subsystemOffset = p.torch_subsystem_offset;
    m_componentOffset = p.torch_component_offset;
    m_componentGuard = p.torch_component_guard;
    m_entityAdjust = p.torch_entity_adjust;
    m_rightOffset = p.torch_right_offset;
    m_upOffset = p.torch_up_offset;
    m_dirOffset = p.torch_dir_offset;
    m_posOffset = p.torch_pos_offset;
    m_multiplier = multiplier;
    m_watchWrites = watchWrites;
    m_narrowRva = p.torch_narrow_rva;
    m_matrixOffset = p.torch_matrix_offset;

    if (m_narrowRva != 0) {
        void* target = const_cast<uint8_t*>(build.base) + m_narrowRva;
        auto& hooks = cameraunlock::hooks::HookManager::Instance();
        auto init = hooks.Initialize();
        if (init == cameraunlock::hooks::HookStatus::ErrorAlreadyInitialized) {
            init = cameraunlock::hooks::HookStatus::Ok;
        }
        if (init == cameraunlock::hooks::HookStatus::Ok &&
            hooks.CreateHook(target, reinterpret_cast<void*>(&NarrowDetour),
                             reinterpret_cast<void**>(&g_narrowOriginal)) ==
                cameraunlock::hooks::HookStatus::Ok &&
            hooks.EnableHook(target) == cameraunlock::hooks::HookStatus::Ok) {
            m_hooked = true;
        } else {
            Log::Line("ERROR: could not hook the matrix narrowing routine at %s+0x%X; the beam "
                      "will not move",
                      kGameModuleName, m_narrowRva);
        }
    }
    m_enabled = true;
    Log::Line("Torch: the beam follows the head at %.2fx on build %s", multiplier, p.name);
}

uintptr_t Torch::Resolve() const {
    uintptr_t g = 0;
    if (!ReadPtr(m_base + m_rootRva, g)) return 0;

    uintptr_t first = 0;
    if (!ReadPtr(g + 0x28, first)) return 0;

    uintptr_t holder = 0;
    if (!ReadPtr(first + 0xE0, holder)) return 0;

    uintptr_t interior = 0;
    if (!ReadPtr(holder, interior)) return 0;
    // The accessor's vtable is read at interior - 0xE0, so the object base is
    // there and not at `interior` itself. Walking from `interior` lands on
    // something that is not vtable-headed and reads null all the way down.
    const uintptr_t player = interior - m_playerAdjust;

    uintptr_t sub = 0;
    if (!ReadPtr(player + m_subsystemOffset, sub)) return 0;

    // The engine's component fetch bails on this, so honour it rather than
    // reading a slot it would have refused.
    uint16_t guard = 0;
    if (!cameraunlock::memory::SafeRead(sub + m_componentGuard, guard) || guard == 0) return 0;

    uintptr_t component = 0;
    if (!ReadPtr(sub + m_componentOffset, component)) return 0;

    uintptr_t entityHolder = 0;
    if (!ReadPtr(component - m_entityAdjust + 0xE0, entityHolder)) return 0;

    uintptr_t torch = 0;
    if (!ReadPtr(entityHolder, torch)) return 0;
    return torch;
}

void Torch::ApplyPerFrame(const CameraBasis& clean, const EngineHeadPose& pose,
                          bool worldSpaceYaw) {
    if (!m_enabled) return;

    const uintptr_t torch = Resolve();
    if (torch == 0) return;   // no lamp yet, or a chain step the engine has not built

    if (m_watchWrites) m_watch.ArmOnce(torch, m_dirOffset);

    // Scaled through the SAME composition the camera used, so the beam and the
    // view cannot disagree about which way the head turned. ScaleHeadEuler is
    // the right one of the core's two scaling shapes here because this mod
    // composes the camera from Euler angles.
    cameraunlock::effects::HeadEuler head;
    head.yaw = pose.yaw;
    head.pitch = pose.pitch;
    head.roll = pose.roll;
    const cameraunlock::effects::HeadEuler led =
        cameraunlock::effects::ScaleHeadEuler(head, m_multiplier);

    EngineHeadPose scaled;
    scaled.yaw = led.yaw;
    scaled.pitch = led.pitch;
    scaled.roll = led.roll;
    const CameraBasis beam = ApplyHeadPose(clean, scaled, worldSpaceYaw);

    if (!IsUsableAxis(beam.forward) || !IsUsableAxis(beam.up) || !IsUsableAxis(beam.right)) {
        return;
    }

    // Published for the detour rather than written here. Writing from this point
    // is what did nothing for two builds: the engine's narrowing routine runs
    // later in the frame and puts the whole basis back.
    g_turn.destination = torch + m_rightOffset;
    g_turn.right[0] = beam.right.x;     g_turn.right[1] = beam.right.y;
    g_turn.right[2] = beam.right.z;
    g_turn.up[0] = beam.up.x;           g_turn.up[1] = beam.up.y;
    g_turn.up[2] = beam.up.z;
    g_turn.forward[0] = beam.forward.x; g_turn.forward[1] = beam.forward.y;
    g_turn.forward[2] = beam.forward.z;
    g_turn.valid = m_hooked;

    if (!m_loggedFirstWrite) {
        m_loggedFirstWrite = true;
        // NOT "the beam is following the head". It is not, and saying so cost a
        // round of testing: the write lands and the engine then recomputes the
        // basis from the aim before the frame is drawn. Measured by forcing a
        // 60-degree yawed basis in from outside and rewriting it every 50ms -
        // it survived 0 of 80 samples and snapped back to the aim each time.
        Log::Line("Torch: the beam basis is being turned in the engine's own narrowing "
                  "routine, after it writes");
    }
}

}  // namespace metroex
