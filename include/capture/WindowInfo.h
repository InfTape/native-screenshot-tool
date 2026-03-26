#pragma once

#include <Windows.h>

#include <string>

namespace capture {

struct WindowInfo {
    HWND handle = nullptr;
    RECT bounds{};
    std::wstring title;
};

}  // namespace capture
