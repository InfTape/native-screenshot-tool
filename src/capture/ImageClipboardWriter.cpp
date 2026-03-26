#include "capture/ImageClipboardWriter.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace capture {

bool ImageClipboardWriter::CopyToClipboard(HWND owner,
                                           const CapturedImage& image,
                                           std::wstring& error_message) const {
    if (image.IsEmpty()) {
        error_message = L"当前没有可复制到剪贴板的截图。";
        return false;
    }

    const std::size_t row_stride = image.RowStride();
    const std::size_t pixel_bytes = row_stride * static_cast<std::size_t>(image.Height());
    const std::size_t total_bytes = sizeof(BITMAPINFOHEADER) + pixel_bytes;
    if (pixel_bytes > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()) ||
        total_bytes > static_cast<std::size_t>(std::numeric_limits<SIZE_T>::max())) {
        error_message = L"图像过大，无法复制到剪贴板。";
        return false;
    }

    BITMAPINFOHEADER info_header{};
    info_header.biSize = sizeof(BITMAPINFOHEADER);
    info_header.biWidth = image.Width();
    info_header.biHeight = image.Height();
    info_header.biPlanes = 1;
    info_header.biBitCount = 32;
    info_header.biCompression = BI_RGB;
    info_header.biSizeImage = static_cast<DWORD>(pixel_bytes);

    const auto& source_pixels = image.Pixels();

    HGLOBAL clipboard_data = GlobalAlloc(GMEM_MOVEABLE, total_bytes);
    if (clipboard_data == nullptr) {
        error_message = L"分配剪贴板内存失败。";
        return false;
    }

    void* locked_memory = GlobalLock(clipboard_data);
    if (locked_memory == nullptr) {
        GlobalFree(clipboard_data);
        error_message = L"锁定剪贴板内存失败。";
        return false;
    }

    auto* destination = static_cast<std::uint8_t*>(locked_memory);
    std::memcpy(destination, &info_header, sizeof(info_header));
    auto* destination_pixels = destination + sizeof(info_header);
    for (int row = 0; row < image.Height(); ++row) {
        const std::size_t src_offset =
            static_cast<std::size_t>(image.Height() - 1 - row) * row_stride;
        const std::size_t dst_offset = static_cast<std::size_t>(row) * row_stride;
        std::memcpy(destination_pixels + dst_offset, source_pixels.data() + src_offset, row_stride);
    }
    GlobalUnlock(clipboard_data);

    if (!OpenClipboard(owner)) {
        GlobalFree(clipboard_data);
        error_message = L"无法打开系统剪贴板。";
        return false;
    }

    if (!EmptyClipboard()) {
        CloseClipboard();
        GlobalFree(clipboard_data);
        error_message = L"清空系统剪贴板失败。";
        return false;
    }

    if (SetClipboardData(CF_DIB, clipboard_data) == nullptr) {
        CloseClipboard();
        GlobalFree(clipboard_data);
        error_message = L"写入剪贴板失败。";
        return false;
    }

    CloseClipboard();
    return true;
}

}  // namespace capture
