#pragma once

#include <Windows.h>

namespace common {

RECT NormalizeRect(const POINT& start, const POINT& end);
RECT ClampRectToBounds(const RECT& rect, int width, int height);
bool HasArea(const RECT& rect);
int RectWidth(const RECT& rect);
int RectHeight(const RECT& rect);

}  // namespace common
