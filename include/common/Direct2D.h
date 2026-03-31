#pragma once

#include <Windows.h>

#include <string>

namespace common {

bool DrawRectangleOnHdc(HDC hdc,
                        const RECT& render_bounds,
                        const RECT* clip_rect,
                        const RECT& rect,
                        COLORREF color,
                        float thickness,
                        std::wstring& error_message);

bool DrawArrowOnHdc(HDC hdc,
                    const RECT& render_bounds,
                    const RECT* clip_rect,
                    const POINT& start,
                    const POINT& end,
                    COLORREF color,
                    float thickness,
                    std::wstring& error_message);

}  // namespace common
