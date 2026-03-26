#include "common/DpiAwareness.h"

#include <Windows.h>

namespace {

using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(int);
using SetProcessDPIAwareFn = BOOL(WINAPI*)();

constexpr int kProcessPerMonitorDpiAware = 2;

}  // namespace

namespace common {

void EnableDpiAwareness() {
    if (const auto set_dpi_context =
            reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
        set_dpi_context != nullptr) {
        if (set_dpi_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }
    }

    if (HMODULE shcore = LoadLibraryW(L"shcore.dll"); shcore != nullptr) {
        const auto set_process_dpi_awareness =
            reinterpret_cast<SetProcessDpiAwarenessFn>(
                GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (set_process_dpi_awareness != nullptr) {
            const HRESULT result = set_process_dpi_awareness(kProcessPerMonitorDpiAware);
            FreeLibrary(shcore);
            if (SUCCEEDED(result)) {
                return;
            }
        } else {
            FreeLibrary(shcore);
        }
    }

    if (const auto set_process_dpi_aware =
            reinterpret_cast<SetProcessDPIAwareFn>(
                GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware"));
        set_process_dpi_aware != nullptr) {
        set_process_dpi_aware();
    }
}

}  // namespace common
