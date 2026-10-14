#include "ui/OcrTextSelection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace {
ocr::Point Lerp(ocr::Point a, ocr::Point b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}
ocr::Quad Slice(const ocr::Quad& quad, float begin, float end) {
    return {Lerp(quad[0], quad[1], begin), Lerp(quad[0], quad[1], end),
            Lerp(quad[3], quad[2], end), Lerp(quad[3], quad[2], begin)};
}
float DistanceSquared(ocr::Point point, const ocr::Quad& quad) {
    bool positive = false, negative = false;
    float area = 0;
    float distance = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < 4; ++i) {
        const auto a = quad[i], b = quad[(i + 1) % 4];
        area += a.x * b.y - b.x * a.y;
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float cross = dx * (point.y - a.y) - dy * (point.x - a.x);
        positive |= cross > 0; negative |= cross < 0;
        const float length = dx * dx + dy * dy;
        const float t = length > 0 ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0.0f, 1.0f) : 0;
        const auto nearest = Lerp(a, b, t);
        const float x = point.x - nearest.x, y = point.y - nearest.y;
        distance = std::min(distance, x * x + y * y);
    }
    return std::abs(area) > 0.001f && !(positive && negative) ? 0 : distance;
}
std::vector<std::size_t> Boundaries(const std::wstring& text, std::size_t begin, std::size_t end) {
    std::vector<std::size_t> offsets{begin};
    while (begin < end) {
        const wchar_t first = text[begin++];
        if (first >= 0xD800 && first <= 0xDBFF && begin < end &&
            text[begin] >= 0xDC00 && text[begin] <= 0xDFFF) ++begin;
        offsets.push_back(begin);
    }
    return offsets;
}
std::wstring_view Trim(std::wstring_view text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring_view::npos) return {};
    const auto last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}
bool EnglishLetter(wchar_t character) {
    return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z');
}
bool StartsListItem(std::wstring_view text) {
    text = Trim(text);
    if (text.empty()) return false;
    if (text.front() == L'•') return true;
    if (text.size() > 1 && (text.front() == L'-' || text.front() == L'*') &&
        (text[1] == L' ' || text[1] == L'\t')) return true;
    std::size_t index = 0;
    while (index < text.size() && text[index] >= L'0' && text[index] <= L'9') ++index;
    return index > 0 && index + 1 < text.size() && (text[index] == L'.' || text[index] == L')') &&
        (text[index + 1] == L' ' || text[index + 1] == L'\t');
}
bool JoinsVisualWrap(const ocr::Line& previous, const ocr::Line& current,
                     std::wstring_view preceding_text, std::wstring_view next_text) {
    if (StartsListItem(next_text)) return false;
    preceding_text = Trim(preceding_text);
    const auto end = preceding_text.find_last_not_of(L"\"'’”)]");
    if (end != std::wstring_view::npos && std::wstring_view(L".!?:;。！？：；").find(preceding_text[end]) != std::wstring_view::npos)
        return false;
    const auto bounds = [](const ocr::Quad& quad) {
        std::array<float, 4> box{quad[0].x, quad[0].y, quad[0].x, quad[0].y};
        for (const auto& point : quad) {
            box[0] = std::min(box[0], point.x); box[1] = std::min(box[1], point.y);
            box[2] = std::max(box[2], point.x); box[3] = std::max(box[3], point.y);
        }
        return box;
    };
    const auto a = bounds(previous.bounds), b = bounds(current.bounds);
    const float previous_height = a[3] - a[1], next_height = b[3] - b[1];
    if (previous_height <= 0 || next_height <= 0) return true;
    const float height = std::max(previous_height, next_height);
    // Larger gaps, a changed text size, and separate columns indicate a new block.
    return height <= 1.4f * std::min(previous_height, next_height) &&
        b[1] - a[1] >= height * 0.5f && b[1] - a[3] <= height * 0.8f &&
        std::abs(b[0] - a[0]) <= height * 2.0f;
}
} // namespace

namespace ui {
void OcrTextSelection::Clear() {
    document_ = {}; characters_.clear(); anchor_.reset(); focus_.reset();
}
void OcrTextSelection::SetDocument(ocr::Document document) {
    Clear();
    document_ = std::move(document);
    std::erase_if(document_.lines, [](const ocr::Line& line) { return line.text.empty(); });
    for (const auto& line : document_.lines) {
        std::vector<Character> chars;
        auto add = [&](std::size_t begin, std::size_t end, const ocr::Quad& bounds) {
            const auto offsets = Boundaries(line.text, begin, end);
            const float count = static_cast<float>(offsets.size() - 1);
            for (std::size_t i = 0; i + 1 < offsets.size(); ++i) {
                chars.push_back({offsets[i], offsets[i + 1], Slice(bounds, static_cast<float>(i) / count,
                    static_cast<float>(i + 1) / count)});
            }
        };
        std::size_t cursor = 0;
        for (const auto& word : line.words) {
            if (word.text.empty()) continue;
            const auto start = line.text.find(word.text, cursor);
            if (start == std::wstring::npos) continue;
            if (start > cursor) {
                // Explicit spaces/punctuation remain selectable between words.
                const auto gap = chars.empty() ? Slice(line.bounds, 0,
                    static_cast<float>(start) / static_cast<float>(line.text.size())) :
                    ocr::Quad{chars.back().bounds[1], word.bounds[0], word.bounds[3], chars.back().bounds[2]};
                add(cursor, start, gap);
            }
            cursor = start + word.text.size();
            add(start, cursor, word.bounds);
        }
        if (cursor < line.text.size()) {
            add(cursor, line.text.size(), Slice(line.bounds,
                static_cast<float>(cursor) / static_cast<float>(line.text.size()), 1));
        }
        characters_.push_back(std::move(chars));
    }
}
bool OcrTextSelection::HasSelection() const { return anchor_ && focus_ && *anchor_ != *focus_; }
std::pair<OcrTextSelection::Position, OcrTextSelection::Position> OcrTextSelection::OrderedSelection() const {
    return *anchor_ < *focus_ ? std::pair{*anchor_, *focus_} : std::pair{*focus_, *anchor_};
}
bool OcrTextSelection::HitText(ocr::Point point) const {
    for (const auto& line : characters_) for (const auto& character : line) {
        if (DistanceSquared(point, character.bounds) <= 9) return true;
    }
    return false;
}
OcrTextSelection::Position OcrTextSelection::CaretAt(ocr::Point point) const {
    Position best{};
    float best_distance = std::numeric_limits<float>::max();
    for (std::size_t line = 0; line < characters_.size(); ++line) {
        for (const auto& character : characters_[line]) {
            const float distance = DistanceSquared(point, character.bounds);
            if (distance >= best_distance) continue;
            best_distance = distance;
            const auto left = Lerp(character.bounds[0], character.bounds[3], 0.5f);
            const auto right = Lerp(character.bounds[1], character.bounds[2], 0.5f);
            const float dx = right.x - left.x, dy = right.y - left.y;
            const float projection = (point.x - left.x) * dx + (point.y - left.y) * dy;
            best = {line, projection > (dx * dx + dy * dy) * 0.5f ? character.end : character.begin};
        }
    }
    return best;
}
void OcrTextSelection::BeginSelection(ocr::Point point) {
    anchor_.reset(); focus_.reset();
    if (HitText(point)) anchor_ = focus_ = CaretAt(point);
}
void OcrTextSelection::ExtendSelection(ocr::Point point) { if (anchor_) focus_ = CaretAt(point); }
void OcrTextSelection::SelectAll() {
    if (Empty()) return;
    anchor_ = Position{0, 0};
    focus_ = Position{document_.lines.size() - 1, document_.lines.back().text.size()};
}
void OcrTextSelection::SelectWord(ocr::Point point) {
    if (!HitText(point)) return;
    const auto caret = CaretAt(point);
    const auto& line = document_.lines[caret.line];
    std::size_t cursor = 0;
    for (const auto& word : line.words) {
        const auto offset = line.text.find(word.text, cursor);
        if (offset == std::wstring::npos || word.text.empty()) continue;
        cursor = offset + word.text.size();
        if (DistanceSquared(point, word.bounds) <= 9) {
            anchor_ = Position{caret.line, offset}; focus_ = Position{caret.line, cursor}; return;
        }
    }
    BeginSelection(point);
}
std::wstring OcrTextSelection::AllText() const {
    std::wstring text;
    for (const auto& line : document_.lines) {
        if (!text.empty()) text += L"\r\n";
        text += line.text;
    }
    return text;
}
std::wstring OcrTextSelection::SelectedText() const {
    if (!HasSelection()) return {};
    const auto [first, last] = OrderedSelection();
    std::wstring text;
    for (std::size_t i = first.line; i <= last.line; ++i) {
        if (i != first.line) text += L"\r\n";
        const auto begin = i == first.line ? first.offset : 0;
        const auto end = i == last.line ? last.offset : document_.lines[i].text.size();
        text += document_.lines[i].text.substr(begin, end - begin);
    }
    return text;
}
std::wstring OcrTextSelection::TranslationText() const {
    if (Empty()) return {};
    const auto [first, last] = HasSelection() ? OrderedSelection() :
        std::pair{Position{0, 0}, Position{document_.lines.size() - 1, document_.lines.back().text.size()}};
    std::wstring text;
    std::wstring_view previous_text;
    std::size_t previous_line = first.line;
    for (std::size_t index = first.line; index <= last.line; ++index) {
        const auto& line = document_.lines[index];
        const auto begin = index == first.line ? first.offset : 0;
        const auto end = index == last.line ? last.offset : line.text.size();
        const auto fragment = Trim(std::wstring_view(line.text).substr(begin, end - begin));
        if (fragment.empty()) continue;
        if (!text.empty()) {
            if (JoinsVisualWrap(document_.lines[previous_line], line, previous_text, fragment)) {
                if (text.back() == L'\u00AD') text.pop_back();
                else if (!(text.size() > 1 && text.back() == L'-' && EnglishLetter(text[text.size() - 2]) &&
                    EnglishLetter(fragment.front()))) text += L' ';
            } else text += L"\r\n";
        }
        text += fragment;
        previous_text = fragment;
        previous_line = index;
    }
    return text;
}
std::vector<ocr::Quad> OcrTextSelection::SelectedQuads() const {
    std::vector<ocr::Quad> quads;
    if (!HasSelection()) return quads;
    const auto [first, last] = OrderedSelection();
    for (std::size_t line = first.line; line <= last.line; ++line) {
        const auto begin = line == first.line ? first.offset : 0;
        const auto end = line == last.line ? last.offset : document_.lines[line].text.size();
        for (const auto& character : characters_[line]) {
            if (character.begin >= begin && character.end <= end) quads.push_back(character.bounds);
        }
    }
    return quads;
}
} // namespace ui
