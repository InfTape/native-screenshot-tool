#pragma once

#include <Windows.h>

#include <string>

#include "common/Result.h"

namespace capture {

class FileClipboardWriter {
public:
    common::Result<void> CopyToClipboard(HWND owner, const std::wstring& path) const;
};

}  // namespace capture
