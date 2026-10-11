#pragma once

#include <filesystem>
#include <optional>
#include <stop_token>

#include "ocr/OneOcrService.h"

namespace ocr {

// The same executable starts in worker mode, without UI, tray or hotkeys.
// Pixels travel in an anonymous inherited mapping; only text/geometry returns.
common::Result<Document> RecognizeInWorker(capture::CapturedImage image,
    std::stop_token stop = {}, const std::filesystem::path& executable = {});

// Call before application initialization. nullopt means normal application mode.
std::optional<int> RunWorkerIfRequested();

} // namespace ocr
