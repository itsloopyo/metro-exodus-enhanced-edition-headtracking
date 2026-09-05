#include "ads_state.h"

#include "build_profile.h"
#include "logging.h"

namespace metroex {

void AdsState::Initialise() {
    const ResolvedBuild build = ResolveRunningBuild();

    // Which way a build mismatch runs is already spelled out once by the
    // game-state matcher, which reads the same header for the same reason.
    // Saying it twice in the log buys nothing, so this only says what the
    // player loses.
    if (const char* cause = BuildLookupCause(build.outcome)) {
        Log::Line("ADS: %s; the sights are never detected, so the ADS mode cycle has nothing to "
                  "act on",
                  cause);
        return;
    }

    const BuildProfile& p = *build.profile;

    // Zero is the registry's "not derived on this build" value, not an address:
    // RVA 0 is the DOS header.
    if (p.ads_flag_rva == 0) {
        Log::Line("ADS: build %s has no aim flag address yet; the sights are never detected, "
                  "so the ADS mode cycle has nothing to act on",
                  p.name);
        return;
    }
    if (!RvaFits(p.ads_flag_rva, sizeof(uint8_t), build.fingerprint.SizeOfImage)) {
        Log::Line("ERROR: build profile %s puts the aim flag outside the image; the sights "
                  "are never detected",
                  p.name);
        return;
    }

    m_flag = reinterpret_cast<const volatile uint8_t*>(build.base + p.ads_flag_rva);
    Log::Line("ADS: build %s recognised; the sights are read from the game's own aim flag, so "
              "it works on weapons whose sights do not magnify",
              p.name);
}

bool AdsState::IsAiming() const { return m_flag != nullptr && *m_flag != 0; }

}  // namespace metroex
