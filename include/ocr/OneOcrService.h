#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <array>
#include <vector>

#include "capture/CapturedImage.h"
#include "common/Result.h"

namespace ocr {

struct Point { float x = 0, y = 0; };
using Quad = std::array<Point, 4>;
static_assert(sizeof(Quad) == 32);
struct Word { std::wstring text; Quad bounds{}; };
struct Line { std::wstring text; Quad bounds{}; std::vector<Word> words; };
struct Document { std::wstring text; std::vector<Line> lines; };

// Uses OneOCR from an installed Photos/Snipping Tool package, or an explicit
// runtime directory. No Microsoft binaries or model weights are bundled.
class OneOcrService {
public:
    explicit OneOcrService(std::filesystem::path runtime_directory = {});
    ~OneOcrService();
    OneOcrService(const OneOcrService&) = delete;
    OneOcrService& operator=(const OneOcrService&) = delete;

    common::Result<std::wstring> Recognize(const capture::CapturedImage& image);
    common::Result<Document> RecognizeDocument(const capture::CapturedImage& image);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ocr
