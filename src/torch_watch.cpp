#include "torch_watch.h"

#include "build_profile.h"
#include "logging.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <atomic>

namespace metroex {

namespace {

std::atomic<uintptr_t> g_watched{0};
std::atomic<bool> g_reported{false};

// This mod's own module bounds. The FIRST run of this watch reported
// MetroExodusHeadTracking.asi+0x17D6C - it had caught Torch::ApplyPerFrame's own
// SafeWrite and then disarmed, so the engine's write was never seen. Our writes
// have to be skipped WITHOUT disarming.
uintptr_t g_selfBase = 0;
uintptr_t g_selfEnd = 0;

bool IsOurOwnWrite(uintptr_t rip) {
    return g_selfBase != 0 && rip >= g_selfBase && rip < g_selfEnd;
}

// Reports the faulting instruction and disarms itself. A data breakpoint raises
// EXCEPTION_SINGLE_STEP AFTER the store has happened, so RIP is the instruction
// following the write - hence the "just before" in the log line rather than a
// claim that this address is the store itself.
LONG CALLBACK OnDebugException(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const uintptr_t faultRip = static_cast<uintptr_t>(info->ContextRecord->Rip);
    if (IsOurOwnWrite(faultRip)) {
        // Ours. Step past it with the breakpoint still armed so the engine's
        // write is the one that gets reported.
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (g_reported.exchange(true)) {
        // Already said it once. Clear the registers on whatever thread this is so
        // the exception does not keep firing, and let the game carry on.
        info->ContextRecord->Dr0 = 0;
        info->ContextRecord->Dr7 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    const uintptr_t rip = faultRip;
    const ResolvedBuild build = ResolveRunningBuild();
    const uintptr_t base = reinterpret_cast<uintptr_t>(build.base);
    if (base != 0 && rip >= base && rip - base < build.fingerprint.SizeOfImage) {
        Log::Line("Torch.Watch: the beam basis was written by code just before "
                  "MetroExodus.exe+0x%llX",
                  static_cast<unsigned long long>(rip - base));
    } else {
        Log::Line("Torch.Watch: the beam basis was written from outside the game image, "
                  "just before 0x%llX",
                  static_cast<unsigned long long>(rip));
    }

    info->ContextRecord->Dr0 = 0;
    info->ContextRecord->Dr7 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// A hardware breakpoint lives in the thread's context, so every thread needs it -
// the write can come from any of them, and on this engine the light update is not
// on the thread the camera hook runs on.
void ArmEveryThread(uintptr_t address) {
    const DWORD self = GetCurrentThreadId();
    const DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    int armed = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                               FALSE, te.th32ThreadID);
        if (th == nullptr) continue;
        if (SuspendThread(th) != static_cast<DWORD>(-1)) {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(th, &ctx)) {
                ctx.Dr0 = address;
                // DR7: enable DR0 locally (bit 0), break on write (bits 16-17 =
                // 01), length 4 bytes (bits 18-19 = 11).
                ctx.Dr7 = (ctx.Dr7 & ~0xFULL) | 0x1ULL;
                ctx.Dr7 = (ctx.Dr7 & ~(0xFULL << 16)) | (0xDULL << 16);
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (SetThreadContext(th, &ctx)) ++armed;
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
    Log::Line("Torch.Watch: watching the beam basis at 0x%llX on %d threads; the next write "
              "will be reported once",
              static_cast<unsigned long long>(address), armed);
}

}  // namespace

void TorchWatch::ArmOnce(uintptr_t torch, uint32_t fieldOffset) {
    if (m_armed || torch == 0) return;
    m_armed = true;

    const uintptr_t address = torch + fieldOffset;
    g_watched.store(address, std::memory_order_release);

    // Learn our own bounds so the handler can tell our writes from the engine's.
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&OnDebugException), &self) &&
        self != nullptr) {
        MODULEINFO mi{};
        if (GetModuleInformation(GetCurrentProcess(), self, &mi, sizeof(mi))) {
            g_selfBase = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
            g_selfEnd = g_selfBase + mi.SizeOfImage;
        }
    }
    m_handler = AddVectoredExceptionHandler(1, OnDebugException);
    if (m_handler == nullptr) {
        Log::Line("Torch.Watch: could not install the exception handler; not watching");
        return;
    }
    ArmEveryThread(address);
}

void TorchWatch::Disarm() {
    if (m_handler != nullptr) {
        RemoveVectoredExceptionHandler(m_handler);
        m_handler = nullptr;
    }
}

}  // namespace metroex
