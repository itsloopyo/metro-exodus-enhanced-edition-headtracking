#pragma once

#include <cstddef>
#include <cstdint>

#include "cameraunlock/memory/safe_memory.h"

namespace metroex {

// The engine's console-variable object, and the one shape every mod-side read of
// one goes through.
//
// The layout is read straight off the constructor, which stores the name at
// +0x08, a pointer to the value at +0x18 and the closed interval its setter
// refuses to leave at +0x20 and +0x24. Two variables are pinned by this mod -
// `r_base_fov_option` in fov.cpp and `g_show_crosshair` in reticle.cpp - and
// they carried their own copies of these offsets and their own name check until
// the two drifted apart on how much of the name they compared.
constexpr uint32_t kCvarNameOffset = 0x08;
constexpr uint32_t kCvarValueOffset = 0x18;
constexpr uint32_t kCvarMinOffset = 0x20;
constexpr uint32_t kCvarMaxOffset = 0x24;

// How much of the object an RVA has to have room for, by what the reader wants
// out of it: the value slot alone, or the bounds as well.
constexpr uint32_t kCvarValueSlotBytes = kCvarValueOffset + sizeof(void*);
constexpr uint32_t kCvarBoundsBytes = kCvarMaxOffset + sizeof(float);

// How long a reader retries a console-variable object for before giving up on
// it.
//
// The engine builds its console-variable table while its statics are still being
// constructed, and an .asi is loaded before that finishes, so a resolve at load
// time reads a half-built object about half the time: the constructor stores the
// name before the value pointer, so the name matches while the pointer is still
// whatever the memory held. Every reader therefore retries from the frame loop
// until the object answers or this window closes. A minute is far past the point
// where the engine has finished its statics, and short enough that a build whose
// variable really has moved says so while the player is still on the main menu.
constexpr uint64_t kCvarResolveWindowMs = 60000;

// What the name at +0x08 said, as three answers rather than two.
//
// "Could not be read" and "read something else" send a bug report to different
// places - the first at the moment the object was read, the second at the
// profile's RVA - and a reader that collapses them tells the wrong story about
// a perfectly good address.
enum class CvarName { Matches, Unreadable, Different };

// Does the object at `cvar` name the variable it is supposed to?
//
// The name is what proves the address is the right object rather than whichever
// bytes now sit at that offset, which matters because the writes that follow
// reach into the engine's own state. The array parameter carries the terminator
// with it, so a longer name that merely starts the same way fails.
//
// Answers Unreadable for a half-built object as readily as for a wrong address,
// which is what the caller's retry turns on.
//
// The name is walked a BYTE AT A TIME rather than read as one N-byte block. A
// block read touches all N bytes at once, so a name whose last character sits on
// the final byte of a committed page faults, and the whole object is then
// reported as carrying the wrong name. Reading byte by byte also stops at the
// first character that differs, so the usual wrong-address case costs one read.
template <size_t N>
CvarName CheckCvarName(uintptr_t cvar, const char (&expected)[N]) {
    const char* name = nullptr;
    if (!cameraunlock::memory::SafeRead(cvar + kCvarNameOffset, name) || name == nullptr) {
        return CvarName::Unreadable;
    }
    const uintptr_t text = reinterpret_cast<uintptr_t>(name);
    for (size_t i = 0; i < N; ++i) {
        uint8_t ch = 0;
        if (!cameraunlock::memory::SafeReadU8(text + i, ch)) return CvarName::Unreadable;
        if (ch != static_cast<uint8_t>(expected[i])) return CvarName::Different;
    }
    return CvarName::Matches;
}

}  // namespace metroex
