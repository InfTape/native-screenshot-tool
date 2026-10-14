#pragma once

#include <stop_token>
#include <string>
#include <string_view>
#include <vector>
#include "common/Result.h"

namespace translation {
// Google Translate's legacy web endpoint (translate_a/single, client=gtx).
// Text is sent directly to Google; no Google Cloud API key is used.
common::Result<std::wstring> TranslateEnglishToChinese(std::wstring_view text, std::stop_token stop = {});
std::wstring BuildGoogleTranslationPath(std::wstring_view text);
common::Result<std::wstring> ParseGoogleTranslationResponse(std::string_view response);
std::vector<std::wstring> SplitTranslationText(std::wstring_view text);
} // namespace translation
