#pragma once

#include <Windows.h>

#include <string>

#include "capture/CapturedImage.h"
#include "capture/ImageClipboardWriter.h"
#include "capture/ImageFileFormat.h"
#include "capture/ImageFileWriter.h"

namespace capture {

struct AutoSaveOptions {
    std::wstring directory;
    ImageFileFormat format = ImageFileFormat::Png;
};

enum class AutoSaveStatus {
    SavedAndCopied,
    SaveFailed,
    ClipboardFailed,
};

struct AutoSaveResult {
    AutoSaveStatus status = AutoSaveStatus::SaveFailed;
    std::wstring saved_path;
};

class AutoSaveService {
public:
    AutoSaveResult SaveImage(HWND owner,
                             const CapturedImage& image,
                             const AutoSaveOptions& options,
                             std::wstring& error_message) const;

private:
    std::wstring BuildFileName(const SYSTEMTIME& local_time) const;
    std::wstring ExtensionForFormat(ImageFileFormat format) const;

    ImageFileWriter image_file_writer_;
    ImageClipboardWriter image_clipboard_writer_;
};

}  // namespace capture
