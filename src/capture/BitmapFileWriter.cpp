#include "capture/BitmapFileWriter.h"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>

namespace capture {

bool BitmapFileWriter::WriteBmp(const std::wstring& path,
                                const CapturedImage& image,
                                std::wstring& error_message) const {
    if (image.IsEmpty()) {
        error_message = L"当前没有可保存的截图。";
        return false;
    }

    const auto row_stride = image.RowStride();
    const auto pixel_bytes = row_stride * static_cast<std::size_t>(image.Height());
    const auto header_bytes = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    const auto file_bytes = header_bytes + pixel_bytes;

    if (pixel_bytes > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()) ||
        file_bytes > static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
        error_message = L"图像过大，无法保存为标准 BMP。";
        return false;
    }

    BITMAPFILEHEADER file_header{};
    file_header.bfType = 0x4D42;
    file_header.bfOffBits = static_cast<DWORD>(header_bytes);
    file_header.bfSize = static_cast<DWORD>(file_bytes);

    BITMAPINFOHEADER info_header{};
    info_header.biSize = sizeof(BITMAPINFOHEADER);
    info_header.biWidth = image.Width();
    info_header.biHeight = image.Height();
    info_header.biPlanes = 1;
    info_header.biBitCount = 32;
    info_header.biCompression = BI_RGB;
    info_header.biSizeImage = static_cast<DWORD>(pixel_bytes);

    const auto& source = image.Pixels();

    std::ofstream output(std::filesystem::path(path), std::ios::binary);
    if (!output.is_open()) {
        error_message = L"无法打开目标文件进行写入。";
        return false;
    }

    output.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
    output.write(reinterpret_cast<const char*>(&info_header), sizeof(info_header));
    for (int row = image.Height(); row-- > 0;) {
        const std::size_t src_offset = static_cast<std::size_t>(row) * row_stride;
        output.write(reinterpret_cast<const char*>(source.data() + src_offset),
                     static_cast<std::streamsize>(row_stride));
    }

    if (!output.good()) {
        error_message = L"写入 BMP 文件时发生错误。";
        return false;
    }

    return true;
}

}  // namespace capture
