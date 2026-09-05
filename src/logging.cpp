#include "logging.h"

#include "path_utils.h"

namespace metroex {

namespace {

// Beside the exe, not beside the .asi, and named for what it is rather than for
// the mod: this is the name the README and the Nexus page tell players to look
// for, and the name uninstall.cmd removes.
constexpr wchar_t kLogFilename[] = L"HeadTracking.log";

}  // namespace

void OpenLog() {
    const std::wstring path = GetExePathW(kLogFilename);
    Log::Open(path);
    Log::Line("Metro Exodus Enhanced Edition Head Tracking v%s attached; logging to %ls",
              METROEX_VERSION, path.c_str());
}

}  // namespace metroex
