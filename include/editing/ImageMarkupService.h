#pragma once

#include <Windows.h>

#include <string>

#include "capture/CapturedImage.h"

namespace editing {

class ImageMarkupService {
public:
    static bool ApplyMosaic(capture::CapturedImage& image,
                            const RECT& region,
                            int block_size,
                            std::wstring& error_message);

    static bool DrawArrow(capture::CapturedImage& image,
                          const RECT& clip_bounds,
                          const POINT& start,
                          const POINT& end,
                          COLORREF color,
                          int thickness,
                          std::wstring& error_message);
};

}  // namespace editing
