#include "path_utils.h"

#include <windows.h>

namespace metroex {

namespace {

// The directory part of whatever `queryName` names, trailing separator kept.
//
// Grown rather than fixed at MAX_PATH: GetModuleFileName truncates to whatever
// buffer it is given, and older Windows reports the truncation only through
// GetLastError, so a deep install directory would silently resolve to a
// directory the file is not in - the log somewhere other than where the player
// is told to look, the INI somewhere other than where install.cmd put it.
template <typename CharT, typename QueryName>
std::basic_string<CharT> DirectoryOf(QueryName queryName, const CharT* separators) {
    std::basic_string<CharT> path(MAX_PATH, CharT{});
    for (;;) {
        const DWORD length = queryName(&path[0], static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            break;
        }
        if (path.size() >= 32768) {
            return {};
        }
        path.resize(path.size() * 2);
    }

    const size_t lastSeparator = path.find_last_of(separators);
    if (lastSeparator == std::basic_string<CharT>::npos) {
        return {};
    }
    return path.substr(0, lastSeparator + 1);
}

}  // namespace

std::string GetExePath(const char* filename) {
    const std::string dir = DirectoryOf<char>(
        [](char* buffer, DWORD size) { return GetModuleFileNameA(nullptr, buffer, size); },
        "\\/");
    if (dir.empty()) {
        // Empty, never the bare filename. The consumer of this path is an INI
        // read or write, and those run through GetPrivateProfileString, which
        // resolves a RELATIVE path against the Windows directory - so returning
        // the filename alone would quietly move the config out of the game
        // folder and read defaults back from a file that is not the player's.
        // An empty path fails the open instead, with the reason in the log.
        return {};
    }
    return dir + filename;
}

std::wstring GetExePathW(const wchar_t* filename) {
    const std::wstring dir = DirectoryOf<wchar_t>(
        [](wchar_t* buffer, DWORD size) { return GetModuleFileNameW(nullptr, buffer, size); },
        L"\\/");
    if (dir.empty()) {
        return {};
    }
    return dir + filename;
}

}  // namespace metroex
