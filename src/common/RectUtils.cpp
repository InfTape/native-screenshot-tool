#include "common/RectUtils.h"

#include <algorithm>

namespace common {

RECT NormalizeRect(const POINT& start, const POINT& end) {
    RECT rect{};
    rect.left = std::min(start.x, end.x);
    rect.top = std::min(start.y, end.y);
    rect.right = std::max(start.x, end.x);
    rect.bottom = std::max(start.y, end.y);
    return rect;
}

RECT ClampRectToBounds(const RECT& rect, int width, int height) {
    RECT clamped{};
    clamped.left = std::clamp(rect.left, 0L, static_cast<LONG>(width));
    clamped.top = std::clamp(rect.top, 0L, static_cast<LONG>(height));
    clamped.right = std::clamp(rect.right, 0L, static_cast<LONG>(width));
    clamped.bottom = std::clamp(rect.bottom, 0L, static_cast<LONG>(height));
    return clamped;
}

bool HasArea(const RECT& rect) {
    return rect.right > rect.left && rect.bottom > rect.top;
}

int RectWidth(const RECT& rect) {
    return static_cast<int>(rect.right - rect.left);
}

int RectHeight(const RECT& rect) {
    return static_cast<int>(rect.bottom - rect.top);
}

}  // namespace common
