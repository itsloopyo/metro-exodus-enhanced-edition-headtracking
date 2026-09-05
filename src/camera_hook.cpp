#include "camera_hook.h"

#include "ads_state.h"
#include "build_profile.h"
#include "config.h"
#include "fov.h"
#include "game_state.h"
#include "logging.h"
#include "reticle.h"
#include "torch.h"
#include "tracking_runtime.h"

#include "cameraunlock/hooks/hook_manager.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <windows.h>

namespace metroex {

namespace {

// Byte offsets into the published camera block. Confirmed in a running game:
// the basis at 0x10/0x20/0x30 satisfies right == cross(up, forward), and the
// view matrix at 0x40 has exactly those three as its columns with its fourth
// row equal to the negated dot products of the position against them.
constexpr size_t kOffsetPosition = 0x00;
constexpr size_t kOffsetForward = 0x10;
constexpr size_t kOffsetUp = 0x20;
constexpr size_t kOffsetRight = 0x30;
constexpr size_t kOffsetViewMatrix = 0x40;

// What the hook itself reads and writes: the position, the basis and the float
// view matrix, which ends at +0x80. Bounds-checked against the image once, so
// nothing below has to ask again. The block runs a long way further - the six
// frustum planes start at +0x200 and the field of view sits at +0x4B4 - so
// anything that grows past the view matrix has to grow this with it.
constexpr uint32_t kBlockBytes = 0x80;

// How much of the builder has to be inside the image before MinHook is pointed
// at it. A detour rewrites the first instructions of the target and relocates
// them into a trampoline, so the check has to cover more than the entry byte.
constexpr uint32_t kHookPatchBytes = 16;

using ViewBuilderFn = void(__fastcall*)(void*, void*, void*, void*);

ViewBuilderFn g_original = nullptr;
uint8_t* g_block = nullptr;

TrackingRuntime* g_tracking = nullptr;
GameState g_gameState;
Torch g_torch;

// How many consecutive foreign-block publishes before saying so. At 60fps this is
// about ten seconds, long enough that ordinary shadow and reflection cameras
// between two main-camera publishes never trip it.
constexpr uint32_t kForeignBlockReportAfter = 600;
uint32_t g_foreignBlockRun = 0;
bool g_reportedForeignBlock = false;
const uint8_t* g_moduleBase = nullptr;
FovState g_fov;
AdsState g_ads;
Reticle g_reticle;
bool g_discovery = false;

bool g_injectedOnce = false;

Vec3 ReadVec3(const uint8_t* base, size_t offset) {
    float v[3];
    std::memcpy(v, base + offset, sizeof(v));
    return Vec3(v[0], v[1], v[2]);
}

void WriteVec3(uint8_t* base, size_t offset, const Vec3& v) {
    const float out[3] = {v.x, v.y, v.z};
    std::memcpy(base + offset, out, sizeof(out));
}

CameraBasis ReadBasis(const uint8_t* base) {
    CameraBasis b;
    b.position = ReadVec3(base, kOffsetPosition);
    b.forward = ReadVec3(base, kOffsetForward);
    b.up = ReadVec3(base, kOffsetUp);
    b.right = ReadVec3(base, kOffsetRight);
    return b;
}

void WriteBasis(uint8_t* base, const CameraBasis& b) {
    WriteVec3(base, kOffsetPosition, b.position);
    WriteVec3(base, kOffsetForward, b.forward);
    WriteVec3(base, kOffsetUp, b.up);
    WriteVec3(base, kOffsetRight, b.right);
}

// The camera the engine ACTUALLY built this frame, read back out of the view
// matrix rather than out of the values the mod asked for. Its columns are the
// basis and its fourth row is the negated dot products of the eye against them,
// so the eye comes back by multiplying that row through the same basis.
//
// This is the diagnostic that answers "did the injection reach the frame". The
// clean basis and the drawn basis are both the mod's own numbers; only this one
// comes from the engine.
CameraBasis ReadBuiltCamera(const uint8_t* base) {
    float m[16];
    std::memcpy(m, base + kOffsetViewMatrix, sizeof(m));
    CameraBasis b;
    b.right = Vec3(m[0], m[4], m[8]);
    b.up = Vec3(m[1], m[5], m[9]);
    b.forward = Vec3(m[2], m[6], m[10]);
    b.position = Vec3(-(m[12] * b.right.x + m[13] * b.up.x + m[14] * b.forward.x),
                      -(m[12] * b.right.y + m[13] * b.up.y + m[14] * b.forward.y),
                      -(m[12] * b.right.z + m[13] * b.up.z + m[14] * b.forward.z));
    return b;
}

// An axis the engine can build a view matrix out of: finite, and long enough to
// be a direction.
//
// Finiteness alone is not the test. Vec3::Normalized() answers Zero() for a
// vector too short to normalise, so a degenerate clean basis - which the block
// holds before the engine's first camera write - comes through ApplyHeadPose as
// three all-zero axes that pass every isfinite() call. The engine would then
// build a singular view matrix and six frustum planes with zero normals, which
// is the black screen this check exists to prevent.
bool IsUsableAxis(const Vec3& v) {
    const float sqrMag = Vec3::Dot(v, v);
    return std::isfinite(sqrMag) && sqrMag > 0.5f;
}

bool IsFinite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

CameraFrame MakeFrame(const CameraBasis& clean, const CameraBasis& drawn,
                      const TrackingState& state) {
    CameraFrame f;
    f.clean = clean;
    f.drawn = drawn;
    f.state = state;
    return f;
}

// Let the engine build its frame from the camera it published, untouched, and
// re-derive the mark against it. Every path that does not apply a head pose ends
// here: the mark is re-derived on those frames too, rather than left standing
// against a camera it no longer describes.
void BuildFromCleanCamera(void* block, void* b, void* c, void* d, const CameraBasis& clean,
                          const TrackingState& state, AdsMode adsMode) {
    g_original(block, b, c, d);
    g_reticle.Update(MakeFrame(clean, clean, state), g_fov.Tangents(), adsMode);
}

void LogDiscovery(const CameraBasis& clean, const CameraBasis& drawn, const CameraBasis& built,
                  const HeadPose& pose, const EngineHeadPose& engine, const TrackingState& state) {
    static uint64_t lastMs = 0;
    const uint64_t now = GetTickCount64();
    if (now - lastMs < 1000) return;
    lastMs = now;
    const HalfFieldTangents t = g_fov.Tangents();
    Log::Line("Camera.Discovery %s aiming=%d | tracker ypr=(%.2f,%.2f,%.2f) xyz=(%.3f,%.3f,%.3f) "
              "haspos=%d -> engine ypr=(%.2f,%.2f,%.2f) xyz=(%.3f,%.3f,%.3f)",
              TrackingVerdictName(state.verdict), state.aiming ? 1 : 0, pose.yaw, pose.pitch,
              pose.roll, pose.x, pose.y, pose.z, pose.has_position ? 1 : 0, engine.yaw,
              engine.pitch, engine.roll, engine.x, engine.y, engine.z);
    Log::Line("Camera.Discovery clean pos=(%.3f,%.3f,%.3f) fwd=(%.4f,%.4f,%.4f) "
              "up=(%.4f,%.4f,%.4f) right=(%.4f,%.4f,%.4f) | drawn pos=(%.3f,%.3f,%.3f) "
              "fwd=(%.4f,%.4f,%.4f) up=(%.4f,%.4f,%.4f) | tan=(%.4f,%.4f) valid=%d",
              clean.position.x, clean.position.y, clean.position.z, clean.forward.x,
              clean.forward.y, clean.forward.z, clean.up.x, clean.up.y, clean.up.z,
              clean.right.x, clean.right.y, clean.right.z, drawn.position.x, drawn.position.y,
              drawn.position.z, drawn.forward.x, drawn.forward.y, drawn.forward.z, drawn.up.x,
              drawn.up.y, drawn.up.z, t.x, t.y, t.valid ? 1 : 0);
    Log::Line("Camera.Discovery built pos=(%.3f,%.3f,%.3f) fwd=(%.4f,%.4f,%.4f) "
              "up=(%.4f,%.4f,%.4f) right=(%.4f,%.4f,%.4f)",
              built.position.x, built.position.y, built.position.z, built.forward.x,
              built.forward.y, built.forward.z, built.up.x, built.up.y, built.up.z,
              built.right.x, built.right.y, built.right.z);
}

void __fastcall ViewBuilderDetour(void* block, void* b, void* c, void* d) {
    if (block != g_block) {
        // Every camera the engine publishes that is not the one this mod owns.
        // A long unbroken run of them means the main camera has moved to another
        // block and the hook is doing nothing - which looks exactly like head
        // tracking "just stopping", with nothing in the log to say why. Report it
        // once, with the address, so the next report is one line instead of a
        // session.
        if (++g_foreignBlockRun == kForeignBlockReportAfter && !g_reportedForeignBlock) {
            g_reportedForeignBlock = true;
            Log::Line("Camera: %u cameras in a row published through a block this mod does not "
                      "own (latest %s+0x%llX, expected +0x%llX). Head tracking is not reaching "
                      "the view.",
                      kForeignBlockReportAfter, kGameModuleName,
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(block) -
                                                      reinterpret_cast<uintptr_t>(g_moduleBase)),
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_block) -
                                                      reinterpret_cast<uintptr_t>(g_moduleBase)));
        }
        g_original(block, b, c, d);
        return;
    }
    g_foreignBlockRun = 0;

    // Everything below runs on the render thread, once per publish of the main
    // camera, which is where the per-frame work of the whole mod belongs: the
    // state reads and the tracker sample have to describe the frame that is
    // about to be built rather than whatever a timer thread last saw.
    g_gameState.Update();
    g_fov.Update();

    const bool aiming = g_ads.IsAiming();
    // Read once for the whole frame, and handed to both consumers. The hotkey
    // thread can advance the cycle between two reads, and a frame that read it
    // twice could feed the pose in one mode and draw the mark in the next.
    const AdsMode adsMode = g_tracking->GetAdsMode();
    HeadPose pose;
    const TrackingState state =
        g_tracking->SamplePerFrame(g_gameState.IsInGameplay(), aiming, adsMode, pose);

    const CameraBasis clean = ReadBasis(g_block);

    if (!PoseApplies(state.verdict)) {
        BuildFromCleanCamera(block, b, c, d, clean, state, adsMode);
        return;
    }

    const EngineHeadPose engine = ToEngineConvention(pose);
    const CameraBasis drawn = ApplyHeadPose(clean, engine, g_tracking->IsWorldSpaceYaw());

    // An unusable basis would reach the view matrix and from there the frustum
    // planes, which culls the whole world and leaves a black screen with nothing
    // in the log. A non-finite POSE cannot get this far - the packet parser
    // rejects a non-finite datagram and every stage after it is bounded - but a
    // degenerate CLEAN basis can, and it arrives through ApplyHeadPose as three
    // all-zero axes that pass every isfinite() call.
    if (!IsFinite(drawn.position) || !IsUsableAxis(drawn.forward) ||
        !IsUsableAxis(drawn.up) || !IsUsableAxis(drawn.right)) {
        BuildFromCleanCamera(block, b, c, d, clean, state, adsMode);
        return;
    }

    // Before the engine builds the frame: the beam is drawn like everything
    // else, so it moves with the head, led by the configured multiplier.
    g_torch.ApplyPerFrame(clean, engine, g_tracking->IsWorldSpaceYaw());

    WriteBasis(g_block, drawn);
    g_original(block, b, c, d);
    const CameraBasis built = g_discovery ? ReadBuiltCamera(g_block) : CameraBasis{};
    // Back to the camera the game believes in, before anything on the gameplay
    // side reads it. The view matrix, the view-projection and the frustum
    // planes the call above just built keep the tracked camera; the position
    // and basis the rest of the engine reads do not.
    WriteBasis(g_block, clean);

    // Against the basis this frame was actually drawn with, not the one before
    // it: the engine has just built its matrices from `drawn`, so that is what
    // the mark has to be projected through.
    g_reticle.Update(MakeFrame(clean, drawn, state), g_fov.Tangents(), adsMode);

    if (!g_injectedOnce) {
        g_injectedOnce = true;
        Log::Line("Camera: head tracking is reaching the view");
    }
    if (g_discovery) LogDiscovery(clean, drawn, built, pose, engine, state);
}

}  // namespace

bool CameraHook::Initialise(const Config& cfg, TrackingRuntime& tracking) {
    g_tracking = &tracking;
    g_discovery = cfg.discovery;

    // All four report their own outcome, including on a build they cannot
    // read, so they are started whether or not the camera hook lands.
    g_gameState.Initialise();
    g_ads.Initialise();
    g_fov.Initialise(cfg);
    g_reticle.Initialise();
    g_torch.Initialise(cfg.light_follows_head, cfg.light_multiplier, cfg.discovery);

    const ResolvedBuild build = ResolveRunningBuild();
    if (const char* cause = BuildLookupCause(build.outcome)) {
        Log::Line("Camera: %s; the view will not move", cause);
        return false;
    }

    g_moduleBase = build.base;
    const BuildProfile& p = *build.profile;
    if (p.camera_block_rva == 0 || p.view_builder_rva == 0) {
        Log::Line("Camera: build %s is recognised but its camera addresses have not been "
                  "derived; the view will not move",
                  p.name);
        return false;
    }
    if (!RvaFits(p.camera_block_rva, kBlockBytes, build.fingerprint.SizeOfImage) ||
        !RvaFits(p.view_builder_rva, kHookPatchBytes, build.fingerprint.SizeOfImage)) {
        Log::Line("ERROR: build profile %s puts the camera outside the image; the view will "
                  "not move",
                  p.name);
        return false;
    }

    g_block = const_cast<uint8_t*>(build.base) + p.camera_block_rva;
    // Local, not file scope: nothing outside this function needs it now that the
    // hook is never removed.
    void* hookTarget = const_cast<uint8_t*>(build.base) + p.view_builder_rva;

    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    auto status = hooks.Initialize();
    // An engine another subsystem already started is started. Treating
    // ErrorAlreadyInitialized as fatal here cost the whole camera hook the moment
    // a second subsystem - the torch - began hooking too, and the log said "the
    // view will not move" while head tracking silently did nothing.
    if (status == cameraunlock::hooks::HookStatus::ErrorAlreadyInitialized) {
        status = cameraunlock::hooks::HookStatus::Ok;
    }
    if (status != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("ERROR: hook engine would not start (%s); the view will not move",
                  cameraunlock::hooks::HookStatusToString(status));
        return false;
    }
    status = hooks.CreateHook(hookTarget, reinterpret_cast<void*>(&ViewBuilderDetour),
                              reinterpret_cast<void**>(&g_original));
    if (status != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("ERROR: could not hook the camera builder at %s+0x%X (%s); the view will "
                  "not move",
                  kGameModuleName, p.view_builder_rva,
                  cameraunlock::hooks::HookStatusToString(status));
        return false;
    }
    status = hooks.EnableHook(hookTarget);
    if (status != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("ERROR: could not enable the camera hook (%s); the view will not move",
                  cameraunlock::hooks::HookStatusToString(status));
        return false;
    }

    Log::Line("Camera: hooked %s+0x%X on build %s", kGameModuleName, p.view_builder_rva, p.name);
    return true;
}

void CameraHook::SettleFieldOfView() {
    g_fov.Update();
    while (!g_fov.OverrideSettled()) {
        Sleep(50);
        g_fov.Update();
    }
}

void CameraHook::RestoreGameState() {
    g_fov.Restore();
    g_reticle.Restore();
}

}  // namespace metroex
