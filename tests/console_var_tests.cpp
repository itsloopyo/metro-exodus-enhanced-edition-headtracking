// The console-variable name check, which is what stands between a build
// profile's RVA and a write into the engine's own state.
//
// Two readers pin a console variable - the field of view and the game's own
// crosshair switch - and each carried its own copy of the check until they
// drifted: one compared the name with its terminator, the other stopped a byte
// short and so matched any longer name that merely started the same way. This
// suite pins the stricter reading, and pins the two cases the retry loop exists
// for: a null name pointer and a name pointer into unmapped memory, both of
// which have to answer Unreadable rather than fault.
//
// Unreadable and Different are separate answers because they send a bug report
// to different places: one at the moment the object was read, the other at the
// profile's RVA.

#include <cstdint>
#include <cstring>

#include <windows.h>

#include "console_var.h"
#include "test_harness.h"

namespace {

using metroex_test::Check;

constexpr char kName[] = "r_base_fov_option";

// The engine object's first three fields, as far as anything here reads it: the
// name at +0x08 and the value pointer at +0x18. Laid out by hand rather than
// declared with members so the offsets under test are the ones being exercised.
struct FakeCvar {
    uint8_t bytes[metroex::kCvarBoundsBytes] = {};

    uintptr_t Address() const { return reinterpret_cast<uintptr_t>(bytes); }

    void SetName(const char* name) {
        std::memcpy(bytes + metroex::kCvarNameOffset, &name, sizeof(name));
    }
};

void TheNameHasToMatchInFull() {
    FakeCvar cvar;
    cvar.SetName(kName);
    Check(metroex::CheckCvarName(cvar.Address(), kName) == metroex::CvarName::Matches,
          "the variable's own name matches");
}

void ALongerNameThatStartsTheSameWayIsRejected() {
    FakeCvar cvar;
    cvar.SetName("r_base_fov_option_2");
    Check(metroex::CheckCvarName(cvar.Address(), kName) == metroex::CvarName::Different,
          "a longer name sharing the prefix is not the same variable");
}

void ADifferentNameIsRejected() {
    FakeCvar cvar;
    cvar.SetName("g_show_crosshair");
    Check(metroex::CheckCvarName(cvar.Address(), kName) == metroex::CvarName::Different,
          "a different variable is rejected");

    FakeCvar shorter;
    shorter.SetName("r_base_fov");
    Check(metroex::CheckCvarName(shorter.Address(), kName) == metroex::CvarName::Different,
          "a shorter name sharing the prefix is rejected");
}

// The half-built object the retry loop exists for: the constructor has not
// written the name pointer yet, so the slot is whatever the memory held.
void AHalfBuiltObjectIsRejectedRatherThanFaulting() {
    FakeCvar unwritten;
    Check(metroex::CheckCvarName(unwritten.Address(), kName) == metroex::CvarName::Unreadable,
          "a null name pointer answers Unreadable");

    FakeCvar garbage;
    garbage.SetName(reinterpret_cast<const char*>(uintptr_t{0x10}));
    Check(metroex::CheckCvarName(garbage.Address(), kName) == metroex::CvarName::Unreadable,
          "a name pointer into unmapped memory answers Unreadable rather than faulting");
}

// A SHORTER name whose terminator is the last byte of a committed page, with
// the next page unmapped.
//
// This is the case the byte-at-a-time walk exists for, and it needs real pages
// to construct: a block read of N bytes from a string shorter than N runs past
// the terminator into the guard page, faults, and the object is reported as
// carrying a DIFFERENT name - which sends a bug report at the build profile's
// RVA when the RVA was right. A byte walk stops at the first character that
// differs, inside the committed page, and answers Different for the right
// reason without ever touching the guard.
void AShortNameAgainstAGuardPageAnswersWithoutFaulting() {
    // Two pages reserved together so they are adjacent, with only the first
    // committed. The second stays reserved-but-unmapped and is the guard.
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const SIZE_T pageSize = info.dwPageSize;

    auto* base = static_cast<char*>(
        VirtualAlloc(nullptr, pageSize * 2, MEM_RESERVE, PAGE_NOACCESS));
    Check(base != nullptr, "two pages could be reserved for the guard-page case");
    if (base == nullptr) return;
    Check(VirtualAlloc(base, pageSize, MEM_COMMIT, PAGE_READWRITE) != nullptr,
          "the first of the two pages could be committed");

    // "r_base_fov" is shorter than "r_base_fov_option", so a block read of the
    // longer name's length from here runs off the end of the committed page.
    static const char kShorter[] = "r_base_fov";
    char* text = base + pageSize - sizeof(kShorter);
    std::memcpy(text, kShorter, sizeof(kShorter));

    FakeCvar cvar;
    std::memcpy(cvar.bytes + metroex::kCvarNameOffset, &text, sizeof(text));
    Check(metroex::CheckCvarName(cvar.Address(), kName) == metroex::CvarName::Different,
          "a short name against a guard page is reported as the wrong name, not as unreadable");

    VirtualFree(base, 0, MEM_RELEASE);
}

}  // namespace

int main() {
    TheNameHasToMatchInFull();
    ALongerNameThatStartsTheSameWayIsRejected();
    ADifferentNameIsRejected();
    AHalfBuiltObjectIsRejectedRatherThanFaulting();
    AShortNameAgainstAGuardPageAnswersWithoutFaulting();
    return metroex_test::Report();
}
