#pragma once

namespace capture {

enum class RecordingFileFormat { Mp4, Gif };

constexpr const wchar_t* RecordingExtension(RecordingFileFormat format) {
    return format == RecordingFileFormat::Gif ? L"gif" : L"mp4";
}

constexpr const wchar_t* RecordingFormatName(RecordingFileFormat format) {
    return format == RecordingFileFormat::Gif ? L"GIF" : L"MP4";
}

constexpr unsigned RecordingFrameRate(RecordingFileFormat format) {
    return format == RecordingFileFormat::Gif ? 15 : 30;
}

}  // namespace capture
