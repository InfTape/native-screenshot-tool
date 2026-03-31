#pragma once

#include <Windows.h>

#include <string>

namespace common {

bool DrawArrowOnHdc(HDC hdc,
                    const RECT& render_bounds,
                    const RECT* clip_rect,
                    const POINT& start,
                    const POINT& end,
                    COLORREF color,
                    float thickness,
                    std::wstring& error_message);

}  // namespace common
