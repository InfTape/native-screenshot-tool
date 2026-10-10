#include "capture/FileClipboardWriter.h"

#include <shlobj.h>

#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>

#include "common/Win32Error.h"

namespace {

struct GlobalMemoryDeleter {
    void operator()(void* memory) const { GlobalFree(memory); }
};
using GlobalMemory = std::unique_ptr<void, GlobalMemoryDeleter>;

struct ClipboardCloser {
    ~ClipboardCloser() { CloseClipboard(); }
};

common::Result<void> Failure(const wchar_t* message) {
    return common::Result<void>::Failure(std::wstring(message) + common::GetLastErrorMessage(GetLastError()));
}

}  // namespace

namespace capture {

common::Result<void> FileClipboardWriter::CopyToClipboard(HWND owner, const std::wstring& path) const {
    if (owner == nullptr || !IsWindow(owner)) {
        return common::Result<void>::Failure(L"复制录屏文件需要有效的窗口。");
    }
    if (path.empty() || path.find(L'\0') != std::wstring::npos) {
        return common::Result<void>::Failure(L"录屏文件路径无效。");
    }
    std::error_code error;
    const auto absolute_path = std::filesystem::absolute(std::filesystem::path(path), error);
    if (error || !std::filesystem::is_regular_file(absolute_path, error) || error) {
        return common::Result<void>::Failure(L"录屏文件不存在或无法访问。");
    }
    const std::wstring file_path = absolute_path.lexically_normal().wstring();
    if (file_path.size() > (std::numeric_limits<SIZE_T>::max() - sizeof(DROPFILES)) / sizeof(wchar_t) - 2) {
        return common::Result<void>::Failure(L"录屏文件路径过长。");
    }

    const UINT effect_format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    if (effect_format == 0) return Failure(L"无法注册文件复制格式：");
    // CF_HDROP contains a Unicode, double-NUL-terminated list of absolute paths.
    GlobalMemory files(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
        sizeof(DROPFILES) + (file_path.size() + 2) * sizeof(wchar_t)));
    GlobalMemory effect(GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD)));
    if (!files || !effect) return Failure(L"无法分配剪贴板数据：");
    auto* drop = static_cast<DROPFILES*>(GlobalLock(files.get()));
    if (drop == nullptr) return Failure(L"无法写入剪贴板文件列表：");
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    std::memcpy(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES),
                file_path.data(), file_path.size() * sizeof(wchar_t));
    GlobalUnlock(files.get());
    auto* preferred_effect = static_cast<DWORD*>(GlobalLock(effect.get()));
    if (preferred_effect == nullptr) return Failure(L"无法写入剪贴板复制方式：");
    *preferred_effect = DROPEFFECT_COPY;
    GlobalUnlock(effect.get());

    // Clipboard viewers may briefly hold the clipboard when a recording ends.
    bool opened = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (OpenClipboard(owner)) {
            opened = true;
            break;
        }
        if (attempt < 5) Sleep(20);
    }
    if (!opened) return Failure(L"无法打开剪贴板，请稍后手动复制录屏文件：");
    ClipboardCloser closer;
    if (!EmptyClipboard()) return Failure(L"无法清空剪贴板：");
    if (!SetClipboardData(CF_HDROP, files.get())) return Failure(L"无法复制录屏文件到剪贴板：");
    files.release();  // Windows owns successful, eagerly rendered clipboard data, even after exit.
    if (!SetClipboardData(effect_format, effect.get())) return Failure(L"无法设置文件复制方式：");
    effect.release();
    return common::Result<void>::Success();
}

}  // namespace capture
