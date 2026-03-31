#pragma once

#include <Windows.h>

#include "common/Result.h"

namespace common {

Result<void> DrawRectangleOnHdc(HDC hdc,
                                const RECT& render_bounds,
                                const RECT* clip_rect,
                                const RECT& rect,
                                COLORREF color,
                                float thickness);

Result<void> DrawArrowOnHdc(HDC hdc,
                            const RECT& render_bounds,
                            const RECT* clip_rect,
                            const POINT& start,
                            const POINT& end,
                            COLORREF color,
                            float thickness);

}  // namespace common
