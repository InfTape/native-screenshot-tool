#pragma once

#include <Windows.h>

#include "capture/CapturedImage.h"
#include "common/Result.h"
#include "editing/MarkupCommand.h"

namespace editing {

class ImageMarkupService {
public:
    static common::Result<void> ApplyCommand(capture::CapturedImage& image,
                                             const MarkupCommand& command);

    static common::Result<void> ApplyMosaic(capture::CapturedImage& image,
                                            const RECT& region,
                                            int block_size);

    static common::Result<void> DrawArrow(capture::CapturedImage& image,
                                          const RECT& clip_bounds,
                                          const POINT& start,
                                          const POINT& end,
                                          COLORREF color,
                                          int thickness);

    static common::Result<void> DrawRectangle(capture::CapturedImage& image,
                                              const RECT& clip_bounds,
                                              const RECT& rect,
                                              COLORREF color,
                                              int thickness);
};

}  // namespace editing
