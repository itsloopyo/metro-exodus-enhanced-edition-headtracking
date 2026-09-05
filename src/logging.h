#pragma once

#include "cameraunlock/logging/file_log.h"

// The process-wide log lives in cameraunlock-core (logging::Open/Close/Line/
// EmergencyLine). Alias it under the mod namespace so call sites read
// Log::Line(...) unqualified.
namespace metroex {
namespace Log = ::cameraunlock::logging;

// Opens HeadTracking.log beside MetroExodus.exe and writes the line every bug
// report starts from: which version of the mod loaded, into which install.
// Call it once, first thing on attach, before anything that logs.
//
// The log is one session long. Core's Open() renames the outgoing session to
// HeadTracking.prev.log and truncates, so the pair never grows past two files
// and the run the player is reporting on is never buried under older ones. The
// previous generation is kept because a crash report is written into the log
// and the player relaunches before fetching it.
void OpenLog();

}
