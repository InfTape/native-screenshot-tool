#include "capture/AutoSaveService.h"

#include <Windows.h>

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace capture {

AutoSaveResult AutoSaveService::SaveImage(HWND owner,
                                          const CapturedImage& image,
                                          const AutoSaveOptions& options,
                                          std::wstring& error_message) const {
    AutoSaveResult result{};
    error_message.clear();

    if (image.IsEmpty()) {
        error_message = L"当前没有可保存的截图。";
        return result;
    }

    if (options.directory.empty()) {
        error_message = L"请先在主窗口设置保存目录。";
        return result;
    }

    std::filesystem::path directory(options.directory);
    try {
        std::filesystem::create_directories(directory);
    } catch (const std::filesystem::filesystem_error&) {
        error_message = L"创建保存目录失败。";
        return result;
    }

    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);

    const std::wstring base_file_name = BuildFileName(local_time);
    const std::wstring extension = ExtensionForFormat(options.format);

    std::filesystem::path output_path = directory / (base_file_name + extension);
    for (int suffix = 1; std::filesystem::exists(output_path); ++suffix) {
        std::wstringstream builder;
        builder << base_file_name << L'_' << std::setw(2) << std::setfill(L'0') << suffix;
        output_path = directory / (builder.str() + extension);
    }

    result.saved_path = output_path.wstring();
    if (!image_file_writer_.Write(result.saved_path, image, options.format, error_message)) {
        result.status = AutoSaveStatus::SaveFailed;
        return result;
    }

    if (!image_clipboard_writer_.CopyToClipboard(owner, image, error_message)) {
        result.status = AutoSaveStatus::ClipboardFailed;
        return result;
    }

    result.status = AutoSaveStatus::SavedAndCopied;
    return result;
}

std::wstring AutoSaveService::BuildFileName(const SYSTEMTIME& local_time) const {
    std::wstringstream builder;
    builder << std::setw(2) << std::setfill(L'0') << (local_time.wYear % 100) << L'-'
            << std::setw(2) << std::setfill(L'0') << local_time.wMonth << L'-'
            << std::setw(2) << std::setfill(L'0') << local_time.wDay << L'-'
            << std::setw(2) << std::setfill(L'0') << local_time.wHour << L'-'
            << std::setw(2) << std::setfill(L'0') << local_time.wMinute << L'-'
            << std::setw(2) << std::setfill(L'0') << local_time.wSecond;
    return builder.str();
}

std::wstring AutoSaveService::ExtensionForFormat(ImageFileFormat format) const {
    return format == ImageFileFormat::Bmp ? L".bmp" : L".png";
}

}  // namespace capture
