#include "fov.h"

#include "build_profile.h"
#include "config.h"
#include "console_var.h"
#include "logging.h"

#include "cameraunlock/memory/safe_memory.h"

#include <cstring>

#include <windows.h>

namespace metroex {

namespace {

constexpr char kBaseFovCvarName[] = "r_base_fov_option";

// The instruction the override removes, and what proves it is that instruction.
//
// The engine eases a live base field of view toward a target once per frame and
// picks that target on the level-state byte - the same byte at +0x6BC that
// game_state.cpp reads, 0 on the main menu and 1 or 2 in a level. The menu's
// target is the r_base_fov_option console variable; a level's is a constant 60
// degrees held in .rdata. One six-byte conditional jump chooses between them,
// and base_fov_pin_rva is where it starts: it is taken in a level, and it lands
// on the load of the constant.
//
// That shape is what RemoveLevelPin checks before it writes anything - the six
// bytes have to decode as a jbe with a 32-bit displacement, and the address it
// jumps to has to begin a rip-relative movss into xmm2 whose operand is a
// plausible field of view.
//
// Six 0x90s in place of that jump leave a level easing toward the console
// variable. The menu takes the same branch already, but its frame delta is 0 so
// nothing eases there either way - the override is a gameplay change only.
constexpr uint32_t kPinBytes = 6;

// The six bytes go in as ONE naturally-aligned 8-byte store, because the
// instruction they replace is being executed while they are written. A memcpy
// of six bytes can be split, and a thread that enters after two of them have
// landed runs `90 90` and then decodes the jump's rel32 operand as an
// instruction, which faults. An aligned 8-byte store on x86-64 is not torn, so
// the executing thread sees either the jump or the NOPs and never half of each.
// A pin whose six bytes do not fit inside one aligned qword is refused rather
// than written unsafely.
constexpr uint32_t kPinStoreBytes = 8;

// Writes `bytes` over the six at `pin` through one aligned 8-byte store,
// leaving the two bytes of the word that are not the instruction as they were.
void StorePinAtomically(uint8_t* pin, const uint8_t (&bytes)[kPinBytes]) {
    const uintptr_t addr = reinterpret_cast<uintptr_t>(pin);
    const uintptr_t base = addr & ~static_cast<uintptr_t>(kPinStoreBytes - 1);
    const size_t lead = static_cast<size_t>(addr - base);
    uint64_t word = 0;
    std::memcpy(&word, reinterpret_cast<const void*>(base), sizeof(word));
    std::memcpy(reinterpret_cast<uint8_t*>(&word) + lead, bytes, kPinBytes);
    // volatile so the compiler emits the single 8-byte store rather than
    // splitting it back into byte moves.
    *reinterpret_cast<volatile uint64_t*>(base) = word;
}

// Whether the six bytes at `pin` sit inside one naturally-aligned 8-byte word.
bool PinFitsOneAlignedStore(const uint8_t* pin) {
    const uintptr_t addr = reinterpret_cast<uintptr_t>(pin);
    return (addr & (kPinStoreBytes - 1)) + kPinBytes <= kPinStoreBytes;
}

// A jbe rel32, and the movss xmm2, [rip+disp32] the taken branch lands on.
constexpr uint8_t kJbeRel32[] = {0x0F, 0x86};
constexpr uint8_t kMovssXmm2RipRel[] = {0xF3, 0x0F, 0x10, 0x15};

// A field of view the pinned branch could plausibly be loading. Deliberately not
// an equality test against 60: the check is here to say the jump lands on code
// that loads a field of view, and a future build is free to pin a different one.
bool IsPlausibleFov(float degrees) {
    return degrees > 1.0f && degrees < 179.0f;
}

}  // namespace

void FovState::Initialise(const Config& cfg) {
    m_overrideFov = cfg.fov_override;

    const ResolvedBuild build = ResolveRunningBuild();
    if (const char* cause = BuildLookupCause(build.outcome)) {
        Log::Line("FOV: %s, so the field of view is neither read nor changed. The reticle has no "
                  "projection to draw with on this build.",
                  cause);
        return;
    }

    const BuildProfile& profile = *build.profile;
    const uint32_t imageSize = build.fingerprint.SizeOfImage;

    if (profile.camera_fov_rva == 0 || profile.camera_aspect_rva == 0 ||
        profile.base_fov_cvar_rva == 0) {
        Log::Line("FOV: build %s is recognised but its field-of-view addresses have not been "
                  "derived, so the field of view is neither read nor changed. The reticle has no "
                  "projection to draw with on this build.",
                  profile.name);
        return;
    }

    if (!RvaFits(profile.camera_fov_rva, sizeof(float), imageSize) ||
        !RvaFits(profile.camera_aspect_rva, sizeof(float), imageSize) ||
        !RvaFits(profile.base_fov_cvar_rva, kCvarBoundsBytes, imageSize)) {
        Log::Line("ERROR: build profile %s has a field-of-view address outside the image; the "
                  "field of view is neither read nor changed",
                  profile.name);
        return;
    }

    m_cameraFov = reinterpret_cast<const volatile float*>(build.base + profile.camera_fov_rva);
    m_cameraAspect =
        reinterpret_cast<const volatile float*>(build.base + profile.camera_aspect_rva);

    // The console variable is not touched here. See the header: the engine's
    // statics are still being constructed at this point, so the object is read
    // from the frame loop until it is finished.
    m_cvar = reinterpret_cast<uintptr_t>(build.base) + profile.base_fov_cvar_rva;
    m_imageBase = build.base;
    m_imageSize = imageSize;
    m_profileName = profile.name;
    if (profile.base_fov_pin_rva != 0) {
        // kPinStoreBytes, not kPinBytes: the write is one aligned eight-byte
        // store, so the checked range has to be the window the store touches
        // rather than the six bytes of the instruction inside it.
        if (RvaFits(profile.base_fov_pin_rva, kPinStoreBytes, imageSize)) {
            m_pin = const_cast<uint8_t*>(build.base) + profile.base_fov_pin_rva;
        } else {
            Log::Line("ERROR: build profile %s puts the field-of-view pin outside the image; "
                      "the override is refused rather than written blind",
                      profile.name);
        }
    }
    m_resolveDeadlineMs = GetTickCount64() + kCvarResolveWindowMs;
    m_resolve = Resolve::Pending;
}

bool FovState::TryResolveBaseFovCvar() {
    switch (CheckCvarName(m_cvar, kBaseFovCvarName)) {
        case CvarName::Matches:
            break;
        case CvarName::Unreadable:
            m_resolveFailure = "the name at that address could not be read";
            return false;
        case CvarName::Different:
            m_resolveFailure = "the object there does not name that variable";
            return false;
    }

    float* value = nullptr;
    if (!cameraunlock::memory::SafeRead(m_cvar + kCvarValueOffset, value) || value == nullptr) {
        m_resolveFailure = "the variable has no value slot";
        return false;
    }

    // The value slot is a static float of the game's own image, so anything
    // outside it is not the field of view and must not be written to. This is
    // also the check the retry above turns on: the name at +0x08 is written
    // before the value pointer at +0x18, so a half-built object matches the name
    // and hands back whatever those eight bytes last held.
    if (!AddressFitsImage(value, m_imageBase, sizeof(float), m_imageSize)) {
        m_resolveFailure = "its value slot is outside the game's own image";
        return false;
    }

    m_baseFovSetting = value;
    m_baseFovMin = reinterpret_cast<float*>(m_cvar + kCvarMinOffset);
    m_baseFovMax = reinterpret_cast<float*>(m_cvar + kCvarMaxOffset);
    return true;
}

bool FovState::RemoveLevelPin() {
    if (m_pin == nullptr) {
        Log::Line("ERROR: build profile %s does not say where the field of view is pinned in a "
                  "level, so the override changes nothing you can see",
                  m_profileName);
        return false;
    }
    if (!PinFitsOneAlignedStore(m_pin)) {
        Log::Line("ERROR: the field-of-view pin on build profile %s straddles two aligned words, "
                  "so it cannot be written without a thread being able to execute half of it; "
                  "the override changes nothing you can see",
                  m_profileName);
        return false;
    }

    uint8_t jump[kPinBytes] = {};
    if (!cameraunlock::memory::SafeRead(reinterpret_cast<uintptr_t>(m_pin), jump) ||
        std::memcmp(jump, kJbeRel32, sizeof(kJbeRel32)) != 0) {
        Log::Line("ERROR: build profile %s does not point at the jump that pins the field of "
                  "view in a level, so nothing is written to the game's code and the override "
                  "changes nothing you can see",
                  m_profileName);
        return false;
    }

    // Where the taken branch goes has to be the load of the pinned field of
    // view. A jbe alone is not enough: the profile is fingerprint-gated, but
    // these six bytes go into the game's own code, and a wrong six is a crash
    // some frames later rather than an error here.
    int32_t rel = 0;
    std::memcpy(&rel, jump + sizeof(kJbeRel32), sizeof(rel));
    const uint8_t* pinned = m_pin + kPinBytes + rel;
    uint8_t load[sizeof(kMovssXmm2RipRel) + sizeof(int32_t)] = {};
    if (!AddressFitsImage(pinned, m_imageBase, sizeof(load), m_imageSize) ||
        !cameraunlock::memory::SafeRead(reinterpret_cast<uintptr_t>(pinned), load) ||
        std::memcmp(load, kMovssXmm2RipRel, sizeof(kMovssXmm2RipRel)) != 0) {
        Log::Line("ERROR: the jump build profile %s calls the field-of-view pin does not go to "
                  "a field-of-view load, so nothing is written to the game's code and the "
                  "override changes nothing you can see",
                  m_profileName);
        return false;
    }

    int32_t valueRel = 0;
    std::memcpy(&valueRel, load + sizeof(kMovssXmm2RipRel), sizeof(valueRel));
    const uint8_t* pinnedValue = pinned + sizeof(load) + valueRel;
    float pinnedFov = 0.0f;
    if (!AddressFitsImage(pinnedValue, m_imageBase, sizeof(pinnedFov), m_imageSize) ||
        !cameraunlock::memory::SafeRead(reinterpret_cast<uintptr_t>(pinnedValue), pinnedFov) ||
        !IsPlausibleFov(pinnedFov)) {
        Log::Line("ERROR: the field-of-view pin build profile %s names does not load a field of "
                  "view, so nothing is written to the game's code and the override changes "
                  "nothing you can see",
                  m_profileName);
        return false;
    }

    DWORD previous = 0;
    if (!VirtualProtect(m_pin, kPinBytes, PAGE_EXECUTE_READWRITE, &previous)) {
        Log::Line("ERROR: the field-of-view pin could not be made writable (%lu); the override "
                  "changes nothing you can see",
                  GetLastError());
        return false;
    }
    uint8_t nops[kPinBytes];
    std::memset(nops, 0x90, sizeof(nops));
    StorePinAtomically(m_pin, nops);
    VirtualProtect(m_pin, kPinBytes, previous, &previous);
    FlushInstructionCache(GetCurrentProcess(), m_pin, kPinBytes);
    return true;
}

void FovState::ApplyOverride() {
    if (m_overrideFov <= 0.0f) {
        // No claim about what the engine holds a level at: that constant lives in
        // .rdata behind the pin and is only ever read by RemoveLevelPin, which
        // does not run on this branch. The cvar's own minimum is not it - the two
        // are both 60 on the builds seen so far and nothing says they must be.
        Log::Line("FOV: build %s recognised; reading the drawn field of view per frame. The "
                  "game's own setting stands at %.1f degrees and its slider allows %.0f to "
                  "%.0f. Set FieldOfView in the ini to go past that.",
                  m_profileName, static_cast<double>(*m_baseFovSetting),
                  static_cast<double>(*m_baseFovMin), static_cast<double>(*m_baseFovMax));
        return;
    }

    // The pin first, and nothing is written to the console variable until it is
    // out of the way. Widening the bounds and writing the value on a build whose
    // pin could not be removed changes nothing the player can see and still
    // leaves their field-of-view slider with a range it did not have.
    if (!RemoveLevelPin()) return;

    // Saved before anything is written, so Restore() can hand the engine back
    // the field of view and the bounds it shipped with. The game reads this
    // variable's value out of its own settings and may write it back on exit,
    // and a value outside the range its own setter accepts is not something to
    // leave behind on a machine the mod has been removed from.
    m_savedSlot = m_baseFovSetting;
    m_savedFov = *m_baseFovSetting;
    m_savedMin = *m_baseFovMin;
    m_savedMax = *m_baseFovMax;

    // Widening to exactly what the override asks for, rather than to a constant
    // of the mod's own, leaves the game's console and its own slider refusing
    // everything they refused before except this one value.
    if (m_overrideFov < *m_baseFovMin) *m_baseFovMin = m_overrideFov;
    if (m_overrideFov > *m_baseFovMax) *m_baseFovMax = m_overrideFov;

    Log::Line("FOV: build %s recognised; overriding the field of view to %.1f degrees in a "
              "level. The engine's own hold at 60 degrees there is lifted for as long as the "
              "game is running; the main menu draws at 60 either way.",
              m_profileName, static_cast<double>(m_overrideFov));
}

void FovState::Restore() {
    if (m_savedSlot == nullptr) return;

    // Guarded, and silent. This runs from DLL_PROCESS_DETACH: every other thread
    // has been terminated where it stood, so the log mutex and the CRT heap lock
    // may both be held by a thread that no longer exists, and a Log::Line here
    // would hang the game on the way out instead of closing it. SafeWrite is SEH
    // over a memcpy and takes neither.
    cameraunlock::memory::SafeWrite(reinterpret_cast<uintptr_t>(m_savedSlot), m_savedFov);
    cameraunlock::memory::SafeWrite(reinterpret_cast<uintptr_t>(m_baseFovMin), m_savedMin);
    cameraunlock::memory::SafeWrite(reinterpret_cast<uintptr_t>(m_baseFovMax), m_savedMax);
    m_savedSlot = nullptr;
}

void FovState::Update() {
    if (m_resolve == Resolve::Pending) {
        if (TryResolveBaseFovCvar()) {
            m_resolve = Resolve::Resolved;
            ApplyOverride();
        } else if (GetTickCount64() >= m_resolveDeadlineMs) {
            m_resolve = Resolve::Failed;
            Log::Line("ERROR: the %s console variable never appeared where build profile %s says "
                      "it is (%s); the field of view is read but cannot be changed",
                      kBaseFovCvarName, m_profileName, m_resolveFailure);
        }
    }

    if (m_overrideFov <= 0.0f || m_baseFovSetting == nullptr) return;
    // The game reads its config file after this mod is loaded, so the first
    // write that sticks is whichever one lands after that. Re-asserting the
    // value costs one compare per frame and removes the ordering question.
    if (*m_baseFovSetting == m_overrideFov) return;
    // Guarded, like every other write this mod makes into engine memory. The
    // value pointer came out of an object the engine was still constructing and
    // is only known to be inside the image, not to be writable, so an unguarded
    // store here is an access violation on the render thread every frame.
    if (!cameraunlock::memory::SafeWrite(reinterpret_cast<uintptr_t>(m_baseFovSetting),
                                         m_overrideFov)) {
        // The pointer is dropped so the fault is not repeated every frame.
        // m_savedSlot is NOT dropped with it: the override has already been
        // written at least once, so the value and bounds still need putting back,
        // and this is the frame that makes putting them back matter most.
        Log::Line("ERROR: the field-of-view setting could not be written and has been dropped; "
                  "the picture stays at whatever the engine last put there");
        m_baseFovSetting = nullptr;
    }
}

HalfFieldTangents FovState::Tangents() const {
    if (m_cameraFov == nullptr || m_cameraAspect == nullptr) return {};
    return TangentsFromCameraFov(*m_cameraFov, *m_cameraAspect);
}

}  // namespace metroex
