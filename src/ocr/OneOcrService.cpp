#include "ocr/OneOcrService.h"

#include <Windows.h>
#include <Shlobj.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.Storage.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr std::array<const wchar_t*, 3> kRuntimeFiles{
    L"oneocr.dll", L"oneocr.onemodel", L"onnxruntime.dll"};

bool HasRuntime(const fs::path& directory) {
    for (const auto* file : kRuntimeFiles) {
        std::error_code error;
        if (!fs::is_regular_file(directory / file, error)) return false;
    }
    return true;
}

fs::path LocalAppData() {
    PWSTR folder = nullptr;
    winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder));
    fs::path result(folder);
    CoTaskMemFree(folder);
    return result;
}

fs::path FindRuntime() {
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
                                          static_cast<DWORD>(executable.size()));
    if (length && length < executable.size()) {
        const auto portable = fs::path(executable.data()).parent_path() / L"ocr";
        if (HasRuntime(portable)) return portable;
    }

    // PackageManager reads the current user's installed package locations and
    // survives Store updates without hard-coding a version or WindowsApps path.
    winrt::Windows::Management::Deployment::PackageManager manager;
    const auto packages = manager.FindPackagesForUser(L"");
    for (const auto* package_name : {L"Microsoft.Windows.Photos", L"Microsoft.ScreenSketch"}) {
        for (const auto& package : packages) {
            if (package.Id().Name() != package_name) continue;
            const fs::path installed(package.InstalledLocation().Path().c_str());
            for (const auto& source : {installed, installed / L"SnippingTool"}) {
                if (!HasRuntime(source)) continue;
                // Ordinary Win32 processes may read WindowsApps files but cannot
                // load its DLLs. Copy to our cache without changing package ACLs.
                const auto cached = LocalAppData() / L"NativeScreenshot" / L"OneOCR" /
                                    package.Id().FullName().c_str();
                fs::create_directories(cached);
                for (const auto* file : kRuntimeFiles) {
                    const auto destination = cached / file;
                    std::error_code error;
                    if (fs::is_regular_file(destination, error) &&
                        fs::file_size(destination) == fs::file_size(source / file)) continue;
                    fs::copy_file(source / file, destination, fs::copy_options::overwrite_existing);
                }
                return cached;
            }
        }
    }
    throw std::runtime_error("未找到 OneOCR。请安装或更新 Windows 照片或截图工具后重试。");
}

std::wstring FromUtf8(const char* text) {
    if (!text || !*text) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (!length) throw std::runtime_error("OCR 返回了无效的文字编码。");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), length);
    result.pop_back();
    return result;
}

struct NativeImage {
    std::int32_t type = 3; // OneOCR BGRA image
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t reserved = 0;
    std::int64_t stride = 0;
    const std::uint8_t* pixels = nullptr;
};
static_assert(sizeof(NativeImage) == 32, "OneOCR integration requires a 64-bit build.");

void Check(std::int64_t status) {
    if (status != 0) {
        throw std::runtime_error("OneOCR 调用失败，错误码 " + std::to_string(status) + "。");
    }
}

struct Apartment {
    Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~Apartment() { winrt::uninit_apartment(); }
};
}  // namespace

namespace ocr {

struct OneOcrService::Impl {
    explicit Impl(fs::path directory) : runtime_directory(std::move(directory)) {}
    ~Impl() { Reset(); }

    template<typename Function>
    Function Bind(const char* name) {
        const auto address = GetProcAddress(module, name);
        if (!address) throw std::runtime_error("OneOCR 版本不兼容：缺少接口 " + std::string(name));
        return reinterpret_cast<Function>(address);
    }

    using CreateOptions = std::int64_t(__cdecl*)(std::int64_t*);
    using Release = void(__cdecl*)(std::int64_t);
    using CreatePipeline = std::int64_t(__cdecl*)(const char*, const char*, std::int64_t, std::int64_t*);
    using DelayLoad = std::int64_t(__cdecl*)(std::int64_t, char);
    using SetLineCount = std::int64_t(__cdecl*)(std::int64_t, std::int64_t);
    using RunPipeline = std::int64_t(__cdecl*)(std::int64_t, const NativeImage*, std::int64_t, std::int64_t*);
    using LineCount = std::int64_t(__cdecl*)(std::int64_t, std::int64_t*);
    using GetLine = std::int64_t(__cdecl*)(std::int64_t, std::int64_t, std::int64_t*);
    using LineContent = std::int64_t(__cdecl*)(std::int64_t, const char**);

    using BoundingBox = std::int64_t(__cdecl*)(std::int64_t, const Quad**);

    void Reset() {
        if (process_options && release_process_options) release_process_options(process_options);
        if (pipeline && release_pipeline) release_pipeline(pipeline);
        if (init_options && release_init_options) release_init_options(init_options);
        process_options = pipeline = init_options = 0;
        if (module) FreeLibrary(module);
        module = nullptr;
        if (onnx_module) FreeLibrary(onnx_module);
        onnx_module = nullptr;
    }

    void Initialize() {
        if (pipeline) return;
        if (runtime_directory.empty()) runtime_directory = FindRuntime();
        if (!HasRuntime(runtime_directory)) {
            throw std::runtime_error("OneOCR 运行文件不完整，需要 oneocr.dll、oneocr.onemodel 和 onnxruntime.dll。");
        }
        try {
            // Only use dependencies next to this DLL or in System32; do not alter
            // the application's global DLL search path.
            // Hold ONNX Runtime explicitly as OneOCR may load it dynamically.
            onnx_module = LoadLibraryExW((runtime_directory / L"onnxruntime.dll").c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!onnx_module) {
                throw std::runtime_error("加载 OCR 推理组件失败，Windows 错误码 " + std::to_string(GetLastError()) + "。");
            }
            module = LoadLibraryExW((runtime_directory / L"oneocr.dll").c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module) {
                throw std::runtime_error("加载 OneOCR 失败，Windows 错误码 " + std::to_string(GetLastError()) + "。");
            }
            release_init_options = Bind<Release>("ReleaseOcrInitOptions");
            release_pipeline = Bind<Release>("ReleaseOcrPipeline");
            release_process_options = Bind<Release>("ReleaseOcrProcessOptions");
            release_result = Bind<Release>("ReleaseOcrResult");
            run_pipeline = Bind<RunPipeline>("RunOcrPipeline");
            line_count = Bind<LineCount>("GetOcrLineCount");
            get_line = Bind<GetLine>("GetOcrLine");
            line_content = Bind<LineContent>("GetOcrLineContent");
            line_bounds = Bind<BoundingBox>("GetOcrLineBoundingBox");
            word_count = Bind<LineCount>("GetOcrLineWordCount");
            get_word = Bind<GetLine>("GetOcrWord");
            word_content = Bind<LineContent>("GetOcrWordContent");
            word_bounds = Bind<BoundingBox>("GetOcrWordBoundingBox");
            Check(Bind<CreateOptions>("CreateOcrInitOptions")(&init_options));
            Check(Bind<DelayLoad>("OcrInitOptionsSetUseModelDelayLoad")(init_options, 0));
            // Model initialization argument from AuroraWright/oneocr's native
            // interface mapping; the installed Microsoft model remains intact.
            constexpr char model_argument[] = "kj)TGtrK>f]b[Piow.gU+nC@s\"\"\"\"\"\"4";
            const auto model = (runtime_directory / L"oneocr.onemodel").u8string();
            Check(Bind<CreatePipeline>("CreateOcrPipeline")(
                reinterpret_cast<const char*>(model.c_str()), model_argument, init_options, &pipeline));
            Check(Bind<CreateOptions>("CreateOcrProcessOptions")(&process_options));
            Check(Bind<SetLineCount>("OcrProcessOptionsSetMaxRecognitionLineCount")(process_options, 1000));
        } catch (...) {
            Reset();
            throw;
        }
    }

    fs::path runtime_directory;
    std::mutex mutex;
    HMODULE module = nullptr;
    HMODULE onnx_module = nullptr;
    std::int64_t init_options = 0, pipeline = 0, process_options = 0;
    Release release_init_options = nullptr, release_pipeline = nullptr;
    Release release_process_options = nullptr, release_result = nullptr;
    RunPipeline run_pipeline = nullptr;
    LineCount line_count = nullptr;
    GetLine get_line = nullptr;
    LineContent line_content = nullptr, word_content = nullptr;
    BoundingBox line_bounds = nullptr, word_bounds = nullptr;
    LineCount word_count = nullptr;
    GetLine get_word = nullptr;

    Quad Bounds(BoundingBox getter, std::int64_t handle) {
        const Quad* bounds = nullptr;
        Check(getter(handle, &bounds));
        if (!bounds) throw std::runtime_error("OCR 未返回文字坐标。");
        Quad copy = *bounds;
        for (const auto& point : copy) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                throw std::runtime_error("OCR 返回了无效的文字坐标。");
        }
        return copy;
    }
};

OneOcrService::OneOcrService(fs::path runtime_directory)
    : impl_(std::make_unique<Impl>(std::move(runtime_directory))) {}
OneOcrService::~OneOcrService() = default;

common::Result<Document> OneOcrService::RecognizeDocument(const capture::CapturedImage& image) {
    if (image.IsEmpty()) return common::Result<Document>::Failure(L"请先选择需要识别的截图区域。");
    if (image.Width() > 10000 || image.Height() > 10000) {
        return common::Result<Document>::Failure(L"识别区域过大，请选择宽高不超过 10000 像素的区域。");
    }
    if (image.Pixels().size() != image.RowStride() * static_cast<std::size_t>(image.Height())) {
        return common::Result<Document>::Failure(L"截图像素数据不完整，请重新截图。");
    }
    try {
        std::lock_guard lock(impl_->mutex);
        Apartment apartment;
        impl_->Initialize();
        // Pad tiny selections and composite straight-alpha window corners over
        // white. This changes only the OCR input, never the captured image.
        const int width = std::max(50, image.Width()), height = std::max(50, image.Height());
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 255);
        for (int y = 0; y < image.Height(); ++y) {
            for (int x = 0; x < image.Width(); ++x) {
                const auto source = static_cast<std::size_t>(y) * image.RowStride() + x * 4;
                const auto destination = (static_cast<std::size_t>(y) * width + x) * 4;
                const unsigned alpha = image.Pixels()[source + 3];
                for (int channel = 0; channel < 3; ++channel) {
                    pixels[destination + channel] = static_cast<std::uint8_t>(
                        (image.Pixels()[source + channel] * alpha + 255 * (255 - alpha) + 127) / 255);
                }
            }
        }
        NativeImage input{3, width, height, 0, static_cast<std::int64_t>(width) * 4, pixels.data()};
        std::int64_t result = 0;
        struct ResultGuard {
            Impl& impl;
            std::int64_t& result;
            ~ResultGuard() { if (result) impl.release_result(result); }
        } guard{*impl_, result};
        Check(impl_->run_pipeline(impl_->pipeline, &input, impl_->process_options, &result));
        if (!result) throw std::runtime_error("OneOCR 未返回识别结果。");
        std::int64_t count = 0;
        Check(impl_->line_count(result, &count));
        if (count < 0 || count > 1000) throw std::runtime_error("OneOCR 返回的行数无效。");
        Document document;
        for (std::int64_t i = 0; i < count; ++i) {
            std::int64_t line = 0;
            Check(impl_->get_line(result, i, &line));
            const char* content = nullptr;
            Check(impl_->line_content(line, &content));
            const auto value = FromUtf8(content);
            if (value.empty()) continue;
            Line item;
            item.text = value;
            item.bounds = impl_->Bounds(impl_->line_bounds, line);
            std::int64_t words = 0;
            Check(impl_->word_count(line, &words));
            if (words < 0 || words > 10000) throw std::runtime_error("OCR 返回的词数无效。");
            for (std::int64_t index = 0; index < words; ++index) {
                std::int64_t word = 0;
                Check(impl_->get_word(line, index, &word));
                const char* word_text = nullptr;
                Check(impl_->word_content(word, &word_text));
                item.words.push_back({FromUtf8(word_text), impl_->Bounds(impl_->word_bounds, word)});
            }
            if (!document.text.empty()) document.text += L"\r\n";
            document.text += value;
            document.lines.push_back(std::move(item));
        }
        return common::Result<Document>::Success(std::move(document));
    } catch (const winrt::hresult_error& error) {
        return common::Result<Document>::Failure(L"无法访问本机 OCR 组件。\n\n" + std::wstring(error.message()));
    } catch (const std::exception& error) {
        return common::Result<Document>::Failure(FromUtf8(error.what()));
    }
}

common::Result<std::wstring> OneOcrService::Recognize(const capture::CapturedImage& image) {
    auto result = RecognizeDocument(image);
    if (!result) return common::Result<std::wstring>::Failure(result.Error());
    return common::Result<std::wstring>::Success(std::move(result.Value().text));
}

}  // namespace ocr
