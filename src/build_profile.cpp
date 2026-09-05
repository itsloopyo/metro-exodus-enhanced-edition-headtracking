#include "build_profile.h"

#include <windows.h>

namespace metroex {

const char* BuildLookupCause(BuildLookup outcome) {
    switch (outcome) {
        case BuildLookup::ModuleNotMapped:
            return "MetroExodus.exe is not mapped";
        case BuildLookup::HeaderUnreadable:
            return "MetroExodus.exe has an unreadable PE header";
        case BuildLookup::NoMatchingProfile:
            return "this MetroExodus.exe matches no known build";
        case BuildLookup::Matched:
            break;
    }
    return nullptr;
}

ResolvedBuild ResolveRunningBuild() {
    ResolvedBuild build;

    auto* module = GetModuleHandleA(kGameModuleName);
    if (module == nullptr) {
        build.outcome = BuildLookup::ModuleNotMapped;
        return build;
    }
    build.base = reinterpret_cast<const uint8_t*>(module);

    if (!cameraunlock::memory::ReadPeFingerprint(module, build.fingerprint)) {
        build.outcome = BuildLookup::HeaderUnreadable;
        return build;
    }

    for (int i = 0; i < kKnownProfileCount; ++i) {
        const BuildProfile& p = kKnownProfiles[i];
        if (build.fingerprint.Matches(FingerprintOf(p))) {
            build.outcome = BuildLookup::Matched;
            build.profile = &p;
            return build;
        }
    }

    build.outcome = BuildLookup::NoMatchingProfile;
    return build;
}

}  // namespace metroex
