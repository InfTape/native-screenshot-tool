#pragma once
#include <Windows.h>

namespace ui {
// Shared resource handles are owned by Windows and must not be destroyed.
inline HICON LoadAppIcon(int width, int height = 0) {
    auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101),
        IMAGE_ICON, width, height ? height : width, LR_SHARED));
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}
} // namespace ui
