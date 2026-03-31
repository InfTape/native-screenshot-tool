#pragma once

#include <Windows.h>

namespace editing {

enum class MarkupTool {
    Select,
    Rectangle,
    Mosaic,
    Arrow,
};

struct MarkupCommand {
    MarkupTool tool = MarkupTool::Select;
    RECT clip_bounds{};
    RECT rect{};
    POINT start{};
    POINT end{};
    COLORREF color = RGB(255, 255, 255);
    int thickness = 0;
    int block_size = 0;
};

}  // namespace editing
