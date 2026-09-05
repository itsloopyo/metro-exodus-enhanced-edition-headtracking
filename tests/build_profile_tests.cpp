// The image-bounds check every pinned address goes through.
//
// This used to be written out at each of the three call sites, in three
// different shapes: `rva >= sizeOfImage` for a byte, `rva + sizeof(T) >
// sizeOfImage` for a word, and a third that added a struct offset first. They
// have to agree, because between them they are what stops a stale profile from
// dereferencing an address outside the game's image. These cases pin the
// equivalence, including the one case none of the three originals survived: an
// RVA close enough to the top of the address space that the sum wraps.

#include <cstdint>
#include <cstring>

#include "build_profile.h"
#include "test_harness.h"

namespace {

using metroex_test::Check;

constexpr uint32_t kImageSize = 0x03233000;

void AByteMustStartInsideTheImage() {
    Check(metroex::RvaFits(kImageSize - 1, sizeof(uint8_t), kImageSize),
          "the last byte of the image is inside it");
    Check(!metroex::RvaFits(kImageSize, sizeof(uint8_t), kImageSize),
          "the byte one past the end is not");
}

// A four-byte read that starts inside the image can still end outside it, which
// is why the size of the read is part of the question rather than the RVA alone.
void AWideReadMustEndInsideTheImageToo() {
    Check(metroex::RvaFits(kImageSize - sizeof(uint32_t), sizeof(uint32_t), kImageSize),
          "a word flush against the end of the image fits");
    Check(!metroex::RvaFits(kImageSize - 1, sizeof(uint32_t), kImageSize),
          "a word starting in the last byte does not, even though its RVA is inside");
}

// The console variable is pinned by its object address and read at a fixed
// offset into it, so the offset counts toward the bound.
void AnOffsetIntoAStructCountsTowardTheBound() {
    constexpr uint32_t kCvarMaxOffset = 0x24;
    Check(!metroex::RvaFits(kImageSize - 0x10, kCvarMaxOffset + sizeof(float), kImageSize),
          "an object near the end whose last field lands past it does not fit");
    Check(metroex::RvaFits(kImageSize - 0x100, kCvarMaxOffset + sizeof(float), kImageSize),
          "and one far enough in does");
}

// 32-bit arithmetic wraps here and reports the read as fitting. No profile
// carries such an RVA, but the check is the last thing between a wrong number in
// the registry and a dereference outside the image, so it must not be the thing
// that hands one through.
void ARvaNearTheTopOfTheAddressSpaceDoesNotWrapIntoTheImage() {
    Check(!metroex::RvaFits(0xFFFFFFFEu, sizeof(uint32_t), kImageSize),
          "an RVA whose read wraps past 4 GiB is outside the image, not inside it");
}

// The same question for an address the engine handed back rather than one the
// mod computed. The field-of-view override writes a float through a pointer read
// out of a console-variable object, and the constructor stores the name it is
// matched on BEFORE that pointer - so a matched name can sit beside a pointer
// that was never written. These cases pin the check that stands between that and
// a float written into whatever address those bytes held.
// A stand-in module base. Nothing is dereferenced here, and the addresses are
// built from integers rather than by walking off the end of a real object, so
// the arithmetic the check has to survive is exercised without relying on
// pointer arithmetic outside an allocation.
constexpr uintptr_t kBase = 0x140000000ull;

const uint8_t* Address(uintptr_t value) { return reinterpret_cast<const uint8_t*>(value); }

void AnEnginePointerInsideTheImageIsAccepted() {
    Check(metroex::AddressFitsImage(Address(kBase), Address(kBase), sizeof(float), kImageSize),
          "the first float of the image is inside it");
    Check(metroex::AddressFitsImage(Address(kBase + kImageSize - sizeof(float)), Address(kBase),
                                    sizeof(float), kImageSize),
          "and so is one flush against the end");
}

void AnEnginePointerOutsideTheImageIsRefused() {
    Check(!metroex::AddressFitsImage(Address(kBase - 1), Address(kBase), sizeof(float), kImageSize),
          "one byte below the module is outside it");
    Check(!metroex::AddressFitsImage(Address(kBase + kImageSize - 1), Address(kBase), sizeof(float),
                                     kImageSize),
          "a float starting in the last byte does not fit, even though its address is inside");
    Check(!metroex::AddressFitsImage(nullptr, Address(kBase), sizeof(float), kImageSize),
          "and a null pointer is not an address in the image");
}

// The distance from the module base to an uninitialised pointer is routinely
// wider than an image size, which is the case a 32-bit subtraction would wrap
// and report as a fit.
void AnEnginePointerGigabytesAwayDoesNotWrapIntoTheImage() {
    Check(!metroex::AddressFitsImage(Address(kBase + 0x100000000ull), Address(kBase), sizeof(float),
                                     kImageSize),
          "an address 4 GiB above the module is outside it, not inside it");
}

// Every address in a profile is a bare uint32_t in a positional initialiser, so
// a field inserted into the struct without the matching move in the profile
// silently shifts every value after it onto the wrong field. The mod's first
// build of the camera hook shipped exactly that and tried to install a hook on
// the aspect-ratio global. Reading the newest profile back BY NAME is what
// catches it, and it has to be the newest one because that is the profile every
// new address lands in first.
void TheNewestProfileCarriesEachAddressOnItsOwnField() {
    const metroex::BuildProfile& p = metroex::kKnownProfiles[0];
    Check(std::strcmp(p.name, "steam-win64-20260827") == 0,
          "the diagnostic primary names itself in the form the log line reports");
    Check(p.timestamp == 0x6A9046B0u, "the diagnostic primary is the 2026-08-27 Steam build");
    Check(p.size_of_image == 0x03234000u, "with the image size that build was linked at");
    Check(p.checksum == 0u, "and the zero checksum the linker leaves on this EXE");
    Check(p.ads_flag_rva == 0u, "ads_flag_rva has not been derived on any build yet");
    Check(p.camera_fov_rva == 0x01703884u, "camera_fov_rva is the drawn vertical field of view");
    Check(p.camera_aspect_rva == 0x01703888u, "camera_aspect_rva is the horizontal multiplier");
    Check(p.camera_block_rva == 0x017033D0u, "camera_block_rva is the published camera block");
    Check(p.view_builder_rva == 0x005C8CB0u, "view_builder_rva is in the code section, not data");
    Check(p.base_fov_cvar_rva == 0x02FE67E8u, "base_fov_cvar_rva is the console-variable object");
    Check(p.base_fov_pin_rva == 0x0581F1Au, "base_fov_pin_rva is the jump that pins it in a level");
    Check(p.crosshair_cvar_rva == 0x02FED3E0u, "crosshair_cvar_rva is the g_show_crosshair object");
    Check(p.level_state_root_rva == 0x01696CA0u, "level_state_root_rva is the engine game object");
    Check(p.level_state_object_offset == 0x08u, "and the chain steps through +0x08");
    // +0x2B, NOT the +0x6BC this profile used to carry. That one reads "no
    // level" while the player walks around a level entered from a save, which is
    // how most people play; it tracks how the session was started. This one was
    // kept because it agreed across three menus and three levels covering both
    // entry paths AND flipped back on a quit to the main menu, which is the test
    // six other candidates failed.
    Check(p.level_state_flag_offset == 0x02Bu, "to the level flags byte at +0x2B");
    Check(p.level_state_flag_offset != 0x6BCu,
          "and never back to +0x6BC, which is not a level flag");
    Check(p.pause_flag_offset == 0x152u, "and the pause byte sits at +0x152 in the same object");
    Check(p.pause_flag_offset != p.level_state_flag_offset,
          "the pause byte and the level flags are different fields");
    Check(p.level_state_vtable_rva == 0x0138BE30u,
          "level_state_vtable_rva is the class whose layout that byte belongs to");
}

// The state chain is the one place in a profile where a HALF-derived entry is
// worse than none: the byte at the end of it sits on exactly one class, and read
// off any other class a zero there is indistinguishable from the main menu, so
// head tracking switches itself off in the middle of a level. Every profile
// therefore carries the type guard with the chain or carries neither.
void NoProfileCarriesTheStateChainWithoutItsTypeGuard() {
    for (int i = 0; i < metroex::kKnownProfileCount; ++i) {
        const metroex::BuildProfile& p = metroex::kKnownProfiles[i];
        Check((p.level_state_root_rva == 0) == (p.level_state_vtable_rva == 0),
              "a profile has both the state chain and its vtable guard, or neither");
        if (p.pause_flag_offset != 0) {
            Check(p.level_state_vtable_rva != 0,
                  "a pause byte is only read under the type guard that says the layout is real");
        }
        if (p.level_state_vtable_rva != 0) {
            Check(p.level_state_vtable_rva >= 0x1046000u,
                  "the guard is a vtable in read-only data, not a code address");
        }
    }
}

// The one mistake the check above cannot make is confusing a code address for a
// data one, so it is worth saying separately: the only RVAs in a profile that
// name code are the view builder and the field-of-view pin, and both have to be
// in .text (RVA 0x1000 to 0x1045000 on this EXE) rather than anywhere in .data.
// Everything else is a global or a console-variable object and lives in .data.
void OnlyTheBuilderAndThePinAreCodeAddresses() {
    for (int i = 0; i < metroex::kKnownProfileCount; ++i) {
        const metroex::BuildProfile& p = metroex::kKnownProfiles[i];
        if (p.view_builder_rva != 0) {
            Check(p.view_builder_rva >= 0x1000u && p.view_builder_rva < 0x1046000u,
                  "the view builder is a code address");
            Check(p.camera_block_rva >= 0x1599000u, "and the camera block is a data address");
        }
        if (p.base_fov_pin_rva != 0) {
            Check(p.base_fov_pin_rva >= 0x1000u && p.base_fov_pin_rva < 0x1046000u,
                  "the field-of-view pin is a code address");
            Check(p.base_fov_cvar_rva >= 0x1599000u,
                  "and the console variable it frees is a data address");
        }
    }
}

// The routing key, read back off each profile through the one function that
// builds it. What this catches is a transposition inside FingerprintOf - the
// image size landing on the checksum field, say - which would leave every
// player matching nothing and the mod dormant for everyone, with every other
// suite still green. It does NOT catch a field reorder in PeFingerprint itself:
// FingerprintOf assigns by name, so a reorder there is invisible to it and to
// the compiler alike.
void EachProfileFingerprintsOntoTheFieldsItDeclares() {
    for (int i = 0; i < metroex::kKnownProfileCount; ++i) {
        const metroex::BuildProfile& p = metroex::kKnownProfiles[i];
        const cameraunlock::memory::PeFingerprint fp = metroex::FingerprintOf(p);
        Check(fp.TimeDateStamp == p.timestamp, "the timestamp routes on the timestamp field");
        Check(fp.SizeOfImage == p.size_of_image, "the image size on the image size field");
        Check(fp.CheckSum == p.checksum, "the checksum on the checksum field");
        Check(fp.Matches(metroex::FingerprintOf(p)), "and a profile matches its own fingerprint");
    }
}

// A profile whose fingerprint duplicates an earlier one is unreachable: the walk
// in ResolveRunningBuild returns the first match, so a copy-pasted timestamp
// strands every player on that build with the wrong offsets and no diagnostic.
void NoTwoProfilesShareAFingerprint() {
    for (int i = 0; i < metroex::kKnownProfileCount; ++i) {
        for (int j = i + 1; j < metroex::kKnownProfileCount; ++j) {
            const cameraunlock::memory::PeFingerprint a =
                metroex::FingerprintOf(metroex::kKnownProfiles[i]);
            Check(!a.Matches(metroex::FingerprintOf(metroex::kKnownProfiles[j])),
                  "two profiles in the registry describe two different builds");
        }
    }
}

// The registry is newest-first, and the top entry is the diagnostic primary the
// "this build is not one I know" line compares against to say whether the
// player's game is newer than the mod or older. Ordered by hand in
// steam_offsets.cpp, so nothing but this enforces it.
void TheRegistryIsOrderedNewestFirst() {
    for (int i = 1; i < metroex::kKnownProfileCount; ++i) {
        Check(metroex::kKnownProfiles[i - 1].timestamp > metroex::kKnownProfiles[i].timestamp,
              "each profile is older than the one above it");
    }
}

// Every build lookup runs through BuildLookupCause, and every caller uses a null
// answer as "this build matched". A cause string on Matched would make every
// subsystem report a failure on a build it can read perfectly.
void OnlyAMatchedLookupHasNoCause() {
    Check(metroex::BuildLookupCause(metroex::BuildLookup::Matched) == nullptr,
          "a matched build has no cause clause");
    Check(metroex::BuildLookupCause(metroex::BuildLookup::ModuleNotMapped) != nullptr,
          "an unmapped module has one");
    Check(metroex::BuildLookupCause(metroex::BuildLookup::HeaderUnreadable) != nullptr,
          "an unreadable header has one");
    Check(metroex::BuildLookupCause(metroex::BuildLookup::NoMatchingProfile) != nullptr,
          "and an unrecognised build has one");
}

// The suite itself does not run inside MetroExodus.exe, so the running-build
// lookup has to report the module as unmapped and hand back no profile. That is
// the same dormancy path a player on an unrecognised build takes, and every
// caller keys off `profile == nullptr` never being true on a Matched outcome.
void AResolveOutsideTheGameIsDormantAndCarriesNoProfile() {
    const metroex::ResolvedBuild build = metroex::ResolveRunningBuild();
    Check(build.outcome != metroex::BuildLookup::Matched,
          "a test binary is not a build of MetroExodus.exe");
    Check(build.profile == nullptr, "so no profile is handed back");
    Check(metroex::BuildLookupCause(build.outcome) != nullptr, "and the outcome has a cause");
}

// The pin is only ever written to on a build that also carries the console
// variable: taking the engine's hold off without a value to put in its place
// would leave the picture easing to whatever the game's own slider last saved,
// which is a change the player did not ask for from a setting they left alone.
void APinnedBuildAlsoCarriesTheConsoleVariable() {
    for (int i = 0; i < metroex::kKnownProfileCount; ++i) {
        const metroex::BuildProfile& p = metroex::kKnownProfiles[i];
        if (p.base_fov_pin_rva == 0) continue;
        Check(p.base_fov_cvar_rva != 0,
              "a profile that pins the field-of-view hold also names the console variable");
    }
}

}  // namespace

int main() {
    TheNewestProfileCarriesEachAddressOnItsOwnField();
    NoProfileCarriesTheStateChainWithoutItsTypeGuard();
    EachProfileFingerprintsOntoTheFieldsItDeclares();
    NoTwoProfilesShareAFingerprint();
    TheRegistryIsOrderedNewestFirst();
    OnlyAMatchedLookupHasNoCause();
    AResolveOutsideTheGameIsDormantAndCarriesNoProfile();
    OnlyTheBuilderAndThePinAreCodeAddresses();
    APinnedBuildAlsoCarriesTheConsoleVariable();
    AByteMustStartInsideTheImage();
    AWideReadMustEndInsideTheImageToo();
    AnOffsetIntoAStructCountsTowardTheBound();
    ARvaNearTheTopOfTheAddressSpaceDoesNotWrapIntoTheImage();
    AnEnginePointerInsideTheImageIsAccepted();
    AnEnginePointerOutsideTheImageIsRefused();
    AnEnginePointerGigabytesAwayDoesNotWrapIntoTheImage();

    return metroex_test::Report();
}
