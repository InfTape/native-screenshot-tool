#pragma once

#include <optional>
#include <utility>
#include "ocr/OneOcrService.h"

namespace ui {

// Coordinates stay in image pixels. The overlay adds its selection origin only
// when painting/hit testing, so monitor DPI never rescales OCR geometry.
class OcrTextSelection {
public:
    void SetDocument(ocr::Document document);
    void Clear();
    bool Empty() const { return document_.lines.empty(); }
    bool HasSelection() const;
    bool HitText(ocr::Point point) const;
    void BeginSelection(ocr::Point point);
    void ExtendSelection(ocr::Point point);
    void SelectWord(ocr::Point point);
    void SelectAll();
    std::wstring SelectedText() const;
    std::vector<ocr::Quad> SelectedQuads() const;

private:
    struct Position {
        std::size_t line = 0, offset = 0;
        auto operator<=>(const Position&) const = default;
    };
    struct Character { std::size_t begin = 0, end = 0; ocr::Quad bounds{}; };
    Position CaretAt(ocr::Point point) const;
    std::pair<Position, Position> OrderedSelection() const;
    ocr::Document document_;
    std::vector<std::vector<Character>> characters_;
    std::optional<Position> anchor_, focus_;
};
} // namespace ui
