#pragma once

#include <Windows.h>

#include <vector>

namespace editing {

enum class MarkupTool {
    Select,
    Rectangle,
    Mosaic,
    Arrow,
    Brush,
};

struct MarkupCommand {
    MarkupTool tool = MarkupTool::Select;
    RECT clip_bounds{};
    RECT rect{};
    POINT start{};
    POINT end{};
    std::vector<POINT> points;
    COLORREF color = RGB(255, 255, 255);
    int thickness = 0;
    int block_size = 0;
};

}  // namespace editing
