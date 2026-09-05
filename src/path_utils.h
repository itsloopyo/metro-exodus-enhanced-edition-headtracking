#pragma once

#include <string>

namespace metroex {

// Beside the running game executable, not beside this DLL. Ultimate ASI Loader
// scans `scripts\` and `plugins\` as well as the exe directory, so the two
// part company whenever the .asi is installed into one of those; the log and the
// config both have to land where the player is told to look for them.
//
// Empty when the directory cannot be resolved: a relative path handed to
// GetPrivateProfileString resolves against the Windows directory rather than the
// game folder, so an empty path that fails the open loudly is the only safe
// answer.
std::string GetExePath(const char* filename);
std::wstring GetExePathW(const wchar_t* filename);

}
