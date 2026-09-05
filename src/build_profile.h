#pragma once

#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

namespace metroex {

// The game module every pinned address in this file is an RVA into.
constexpr char kGameModuleName[] = "MetroExodus.exe";

// One shipped build of MetroExodus.exe, identified by its PE header, plus the static
// addresses this mod reads out of it.
//
// The fingerprint is what routes: TimeDateStamp, SizeOfImage and CheckSum
// together are unique per built EXE, and three independent fields mean a
// repacked binary fails the match instead of being mis-routed onto offsets that
// no longer mean anything. Version strings are not visible from inside the
// process in any reliable way; the PE header is.
//
// The registry is APPEND-ONLY. When a patch moves these addresses, add a new
// profile at the top of kKnownProfiles and leave every older one in place: a
// player who has not taken the patch keeps matching their old profile, and both
// builds work from the same mod binary.
//
// ZERO IN ANY RVA FIELD MEANS NOT DERIVED on that build. RVA 0 is the DOS
// header, never anything this mod wants, so a profile can carry the addresses
// that have been derived and leave the rest at zero. Each reader switches its
// own feature off rather than reading the DOS header and believing what it
// finds there, so a partly-derived build loses one feature instead of the mod.
struct BuildProfile {
    const char* name;

    uint32_t timestamp;
    uint32_t size_of_image;
    uint32_t checksum;

    // Whether a level owns the screen or the main menu does, as one FLAGS byte
    // at the end of a two-step pointer chain: the global at
    // `level_state_root_rva` holds a pointer, `level_state_object_offset` into
    // that object holds another, and `level_state_flag_offset` into THAT is the
    // byte. Bit 0 set means a level is up, bit 1 set means none is; the rest of
    // the byte is not ours to read. Zero in the root leaves the state unread and
    // head tracking on everywhere.
    //
    // `level_state_vtable_rva` IS NOT OPTIONAL DECORATION - it is the engine's
    // own guard on the object, and reading a field without it is reading a
    // layout that may not be there. The object is polymorphic: the engine loads
    // its vtable pointer, compares it against this exact address, and only reads
    // its fields on a match. A mismatch means the state is UNKNOWN, not "no
    // level", and the reader leaves head tracking on rather than guessing from
    // another class's layout. Zero here switches the whole chain off, because a
    // chain with no type guard is a defect this field exists to close.
    //
    // HOW THE OFFSET WAS DERIVED, because the obvious-looking field at +0x6BC is
    // WRONG and cost this mod two sessions of head tracking that silently did
    // nothing. +0x6BC is the byte the engine's own field-of-view easing reads,
    // and it reads "no level" while the player walks around a level entered from
    // a SAVE - it tracks how the session was started, not what is on screen.
    //
    // This offset was found by snapshotting the object across menus and levels
    // and keeping only bytes that agree across THREE independent menus, agree
    // across THREE levels covering BOTH entry paths (two save-loaded, one
    // -map), and differ between the two groups. That left seven. Six of those
    // seven kept their level value after a quit to the main menu - level
    // residue, not state. Only this one flipped back, in the same sample the
    // camera returned to the menu. A candidate that has not been watched through
    // a level -> menu transition has not been tested at all.
    uint32_t level_state_root_rva;
    uint32_t level_state_object_offset;
    uint32_t level_state_flag_offset;
    uint32_t level_state_vtable_rva;

    // A second byte in the SAME object, non-zero while the game is paused - the
    // pause menu, and whatever else the engine counts as suspending play. 1 is
    // paused and 0 is not; any other value is unknown and suppresses nothing.
    //
    // Found the same way as the level flag and with the same trap avoided. A
    // frozen camera means "paused" only INSIDE a live level: on a loading screen
    // it is frozen too, and a first attempt spent every one of its paused
    // samples there, which made the whole scan a load-versus-level diff wearing
    // a pause-versus-play label and returned 19367 candidates. Gating the
    // sampler on the level flag above, and requiring the two states to
    // ALTERNATE, brought that to three. Two of the three then read non-zero in
    // an UNPAUSED level captured in another session and were dropped; a false
    // positive here suppresses head tracking during play, which is the failure
    // this mod has already shipped twice.
    //
    // Zero leaves the pause undetected and head tracking live behind the pause
    // menu, which is what the mod did before this field existed.
    uint32_t pause_flag_offset;

    // Byte, non-zero while the player has the sights up. Read once per frame by
    // AdsState; see ads_state.h for why the flag rather than the view's zoom.
    // Zero leaves the sights undetected, which switches the ADS mode cycle off
    // rather than guessing at it.
    uint32_t ads_flag_rva;

    // The vertical field of view, in degrees, of the frame being drawn, and the
    // multiplier that turns its half-field tangent into the horizontal one. The
    // engine writes both once per camera update and reads them back for its own
    // screen-to-world unprojection, so they describe the frame the player is
    // looking at rather than any setting - see fov.h.
    uint32_t camera_fov_rva;
    uint32_t camera_aspect_rva;

    // The published camera block, and the engine function that rebuilds
    // everything derived from it.
    //
    // The block is what 4A hands its renderer once per frame:
    //
    //   +0x000 position          +0x040 view matrix, float
    //   +0x010 forward           +0x080 projection matrix, float
    //   +0x020 up                +0x0C0 view-projection matrix, float
    //   +0x030 right             +0x100 view matrix, double
    //                            +0x200 six frustum planes
    //
    // `view_builder_rva` takes the block in RCX and derives the view matrix,
    // the view-projection and the frustum planes from the position and basis
    // above. Hooking it is what makes head tracking cull correctly: the frustum
    // the renderer tests against is extracted from the same view-projection the
    // frame is drawn with, so a head turn that brings scenery into view brings
    // it into the frustum in the same breath. Injecting anywhere later would
    // leave the engine culling to the un-turned frustum and the picture would
    // lose its edges.
    //
    // Zero in either leaves the camera untouched: no hook is installed and the
    // view does not move.
    uint32_t camera_block_rva;
    uint32_t view_builder_rva;

    // The `r_base_fov_option` console variable OBJECT, not its value. The
    // object carries its name at +0x08, a pointer to its value at +0x18 and the
    // bounds its setter enforces at +0x20 and +0x24, so this one address routes
    // both the field of view the game's own slider sets and the 60-to-75 range
    // the override has to widen to get past. Pinning the object rather than the
    // value also makes the profile self-checking: the name at +0x08 has to read
    // back as "r_base_fov_option" or the address is wrong and nothing is
    // written.
    uint32_t base_fov_cvar_rva;

    // The conditional jump that stops the console variable above from reaching
    // the picture while a level is loaded.
    //
    // The engine eases a live base field of view toward a target once per frame,
    // and picks that target from the level-state byte: on the main menu the
    // target is the console variable, and in a level it is a constant 60
    // degrees in .rdata. So the game's own Field of View slider, and anything
    // that writes what it writes, moves the picture on the menu and nowhere
    // else. This RVA is the `jbe` that chooses the constant.
    //
    // The override in fov.cpp replaces those six bytes with NOPs, which leaves
    // the engine easing toward its own console variable in a level exactly as it
    // already does on the menu. It is checked before it is written: the bytes
    // have to be a `jbe rel32` whose target loads a plausible field of view out
    // of the image. Zero leaves the code untouched and the override reduced to
    // what it can do without it, which on this engine is nothing in gameplay.
    uint32_t base_fov_pin_rva;

    // The `g_show_crosshair` console variable object, in the same shape: name at
    // +0x08, value pointer at +0x18. The mod writes zero through it while it is
    // drawing its own mark, so the player is looking at one reticle rather than
    // two. Zero here leaves the mod's mark undrawn rather than drawn beside the
    // game's - see reticle.cpp.
    uint32_t crosshair_cvar_rva;

    // The player's torch - 4A calls it the backlight - reached the way the
    // engine's own `entity_players_torch` accessor reaches it. Every step is a
    // plain read: the two "virtual calls" in that accessor are both
    // `mov rax, rcx; ret` behind adjustor thunks, so nothing is invoked.
    //
    //   g         = *(module + torch_root_rva)
    //   holder    = *(*(g + 0x28) + 0xE0)
    //   player    = *holder - torch_player_adjust
    //   sub       = *(player + torch_subsystem_offset)
    //   component = *(sub + torch_component_offset)   guarded by the short at
    //                                                 torch_component_guard
    //   torch     = *(*(component - torch_entity_adjust + 0xE0))
    //
    // Inside the torch is a full BASIS, not a lone direction:
    //
    //   torch_right_offset  right   (+0x30)
    //   torch_up_offset     up      (+0x40)
    //   torch_dir_offset    forward (+0x50)  - equals the camera aim, measured
    //   torch_pos_offset    position(+0x60)  - equals the camera eye, measured
    //
    // ALL THREE AXES HAVE TO BE WRITTEN. Writing the forward alone leaves right
    // and up pointing where they were, so the rotation is inconsistent and the
    // renderer builds the beam from a basis that has not turned. The write lands
    // - a sentinel put in the forward is overwritten within a frame and the mod's
    // own value stands - and the beam still does not move. That was the first
    // attempt at this feature.
    //
    // DO NOT use player + 0x27B8. That is the accessor's FALLBACK branch, taken
    // only when the component lookup yields nothing, and it reads null in a live
    // session with the lamp lit.
    //
    // Zero in the root leaves the torch alone entirely.
    uint32_t torch_root_rva;
    uint32_t torch_player_adjust;
    uint32_t torch_subsystem_offset;
    uint32_t torch_component_offset;
    uint32_t torch_component_guard;
    uint32_t torch_entity_adjust;
    // The engine's double-to-float matrix narrowing routine, and the double
    // matrix it reads for the torch.
    //
    // The float basis at torch+0x30 is a COPY. This routine reads doubles from
    // its second argument and cvtpd2ps's them into the float basis, and it is the
    // LAST writer in the frame - which is why writing the float basis from the
    // camera hook is undone every frame, and why writing the double source is
    // undone too (that is derived from further up in turn).
    //
    // So the mod hooks this, lets it run, and turns the result when the
    // destination is the torch's basis. It is a generic routine called for many
    // objects, so the detour filters on the destination exactly as the camera
    // hook filters on the camera block.
    uint32_t torch_narrow_rva;
    uint32_t torch_matrix_offset;

    uint32_t torch_right_offset;
    uint32_t torch_up_offset;
    uint32_t torch_dir_offset;
    uint32_t torch_pos_offset;
};

// Newest first. The top entry is also the diagnostic primary: when nothing
// matches, its timestamp is what says whether the player's build is newer than
// this mod knows about or older.
extern const BuildProfile* const kKnownProfiles;
extern const int kKnownProfileCount;

// Why a build lookup ended the way it did. Each subsystem words its own log line
// from this - what the player loses when the sights go undetected is not what
// they lose when the field of view cannot be read - so the outcome is reported
// rather than logged here.
enum class BuildLookup {
    Matched,
    ModuleNotMapped,
    HeaderUnreadable,
    NoMatchingProfile,
};

// Why a lookup did not match, as the opening clause of a log line each caller
// finishes with what its own subsystem loses. Null when the build matched.
const char* BuildLookupCause(BuildLookup outcome);

struct ResolvedBuild {
    BuildLookup outcome = BuildLookup::ModuleNotMapped;

    // The module base every RVA in `profile` is measured from. Null unless the
    // module was mapped.
    const uint8_t* base = nullptr;

    // Read whenever the module was mapped and its header was readable, so the
    // NoMatchingProfile line can say which way the mismatch runs.
    cameraunlock::memory::PeFingerprint fingerprint{};

    // Null unless the outcome is Matched.
    const BuildProfile* profile = nullptr;
};

// The fingerprint a profile pins, as the routing key it is compared against.
// Written once here rather than aggregate-initialised at each use, so a change
// to PeFingerprint's field order cannot silently re-order a profile into a
// different build.
inline cameraunlock::memory::PeFingerprint FingerprintOf(const BuildProfile& profile) {
    cameraunlock::memory::PeFingerprint fp{};
    fp.TimeDateStamp = profile.timestamp;
    fp.SizeOfImage = profile.size_of_image;
    fp.CheckSum = profile.checksum;
    return fp;
}

// Fingerprints the running game and looks it up in the registry above. Every
// subsystem that reads a pinned address goes through this, so there is one
// definition of what "this is the build I know" means rather than one per
// reader that can drift from the others.
ResolvedBuild ResolveRunningBuild();

// Whether `byteCount` bytes at `rva` land inside an image of `sizeOfImage`
// bytes. Widened to 64 bits so the sum cannot wrap past the end of the image
// and read back as a fit.
inline bool RvaFits(uint32_t rva, uint32_t byteCount, uint32_t sizeOfImage) {
    return static_cast<uint64_t>(rva) + byteCount <= sizeOfImage;
}

// The same question for an address the mod did not compute from an RVA but read
// out of the game's own memory - a pointer field inside an engine object. A
// profile's RVAs are checked once against the image; a pointer the engine hands
// back has to be checked the same way before anything is written through it,
// because nothing about reading it says it was ever initialised.
inline bool AddressFitsImage(const void* address, const uint8_t* base, uint32_t byteCount,
                             uint32_t sizeOfImage) {
    if (address == nullptr || base == nullptr) return false;
    const uintptr_t a = reinterpret_cast<uintptr_t>(address);
    const uintptr_t b = reinterpret_cast<uintptr_t>(base);
    if (a < b) return false;
    // Narrowed only once the distance is known to be inside a 32-bit image size,
    // so an address gigabytes above the module cannot wrap into it.
    const uintptr_t offset = a - b;
    if (offset > sizeOfImage) return false;
    return RvaFits(static_cast<uint32_t>(offset), byteCount, sizeOfImage);
}

}
