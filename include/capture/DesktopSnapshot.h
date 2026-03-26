#pragma once

#include "capture/CapturedImage.h"

namespace capture {

struct DesktopSnapshot {
    int origin_x = 0;
    int origin_y = 0;
    CapturedImage image;
};

}  // namespace capture
