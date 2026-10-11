#include "ocr/OcrWorker.h"

#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::uint32_t kMagic = 0x52434F4E; // NOCR
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kMaxResponse = 32 * 1024 * 1024;
constexpr auto kTimeout = std::chrono::seconds(120);
struct InputHeader {
    std::uint32_t magic = kMagic, version = kVersion;
    std::int32_t width = 0, height = 0;
};
struct ResponseHeader {
    std::uint32_t magic = kMagic, version = kVersion, failed = 0, size = 0;
};
static_assert(sizeof(InputHeader) == 16 && sizeof(ResponseHeader) == 16);

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { Reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE Get() const { return value_; }
    void Reset(HANDLE value = nullptr) {
        if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = value;
    }
private:
    HANDLE value_;
};
struct View {
    void* data = nullptr;
    ~View() { if (data) UnmapViewOfFile(data); }
    void Reset() { if (data) UnmapViewOfFile(data); data = nullptr; }
};
struct Attributes {
    std::vector<std::uint8_t> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    Attributes() {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        storage.resize(size);
        list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &size))
            throw std::runtime_error("Cannot initialize worker handle list");
    }
    ~Attributes() { if (list) DeleteProcThreadAttributeList(list); }
};
class Packet {
public:
    std::vector<std::uint8_t> bytes;
    template<class T> void Add(const T& value) {
        AddBytes(&value, sizeof(value));
    }
    void AddBytes(const void* data, std::size_t size) {
        if (size > kMaxResponse - bytes.size()) throw std::runtime_error("OCR response too large");
        const auto offset = bytes.size();
        bytes.resize(offset + size);
        if (size) std::memcpy(bytes.data() + offset, data, size);
    }
    void Text(const std::wstring& text) {
        if (text.size() > kMaxResponse / sizeof(wchar_t)) throw std::runtime_error("OCR text too large");
        Add(static_cast<std::uint32_t>(text.size()));
        AddBytes(text.data(), text.size() * sizeof(wchar_t));
    }
};
class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    template<class T> T Get() {
        if (sizeof(T) > bytes_.size() - position_) throw std::runtime_error("Truncated OCR result");
        T value{};
        std::memcpy(&value, bytes_.data() + position_, sizeof(T));
        position_ += sizeof(T);
        return value;
    }
    std::wstring Text() {
        const auto size = Get<std::uint32_t>();
        if (size > (bytes_.size() - position_) / sizeof(wchar_t)) throw std::runtime_error("Truncated OCR text");
        std::wstring text(size, L'\0');
        if (size) std::memcpy(text.data(), bytes_.data() + position_, size * sizeof(wchar_t));
        position_ += size * sizeof(wchar_t);
        return text;
    }
    ocr::Quad Bounds() {
        const auto quad = Get<ocr::Quad>();
        for (const auto& point : quad) if (!std::isfinite(point.x) || !std::isfinite(point.y))
            throw std::runtime_error("Invalid OCR geometry");
        return quad;
    }
    bool Finished() const { return position_ == bytes_.size(); }
private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

Packet Serialize(const common::Result<ocr::Document>& result) {
    Packet packet;
    if (!result) { packet.Text(result.Error()); return packet; }
    packet.Add(static_cast<std::uint32_t>(result.Value().lines.size()));
    for (const auto& line : result.Value().lines) {
        packet.Text(line.text);
        packet.Add(line.bounds);
        packet.Add(static_cast<std::uint32_t>(line.words.size()));
        for (const auto& word : line.words) { packet.Text(word.text); packet.Add(word.bounds); }
    }
    return packet;
}
common::Result<ocr::Document> Deserialize(const ResponseHeader& header, const std::vector<std::uint8_t>& bytes) {
    Reader reader(bytes);
    if (header.failed) {
        auto error = reader.Text();
        if (!reader.Finished()) throw std::runtime_error("Unexpected OCR error data");
        return common::Result<ocr::Document>::Failure(std::move(error));
    }
    ocr::Document document;
    const auto count = reader.Get<std::uint32_t>();
    if (count > 1000) throw std::runtime_error("Invalid OCR line count");
    for (std::uint32_t i = 0; i < count; ++i) {
        ocr::Line line;
        line.text = reader.Text(); line.bounds = reader.Bounds();
        const auto words = reader.Get<std::uint32_t>();
        if (words > 10000) throw std::runtime_error("Invalid OCR word count");
        for (std::uint32_t j = 0; j < words; ++j) line.words.push_back({reader.Text(), reader.Bounds()});
        if (!document.text.empty()) document.text += L"\r\n";
        document.text += line.text;
        document.lines.push_back(std::move(line));
    }
    if (!reader.Finished()) throw std::runtime_error("Unexpected OCR result data");
    return common::Result<ocr::Document>::Success(std::move(document));
}
bool WriteAll(HANDLE pipe, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    while (size) {
        DWORD written = 0;
        if (!WriteFile(pipe, bytes, static_cast<DWORD>(std::min<std::size_t>(size, 65536)), &written, nullptr) || !written) return false;
        bytes += written; size -= written;
    }
    return true;
}
void ReadAll(HANDLE pipe, HANDLE process, void* data, std::size_t size,
             const std::stop_token& stop, std::chrono::steady_clock::time_point deadline) {
    auto* bytes = static_cast<std::uint8_t*>(data);
    while (size) {
        if (stop.stop_requested()) throw std::runtime_error("OCR cancelled");
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("OCR timed out");
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) throw std::runtime_error("OCR worker disconnected");
        if (!available) {
            if (WaitForSingleObject(process, 25) == WAIT_OBJECT_0) {
                if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available)
                    throw std::runtime_error("OCR worker exited without a result");
            } else continue;
        }
        DWORD read = 0;
        const auto wanted = static_cast<DWORD>(std::min<std::size_t>(size, available));
        if (!ReadFile(pipe, bytes, wanted, &read, nullptr) || !read) throw std::runtime_error("OCR result read failed");
        bytes += read; size -= read;
    }
}
std::filesystem::path CurrentExecutable() {
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length == path.size()) throw std::runtime_error("Cannot locate executable");
    return path.data();
}
HANDLE ParseHandle(const wchar_t* text) {
    wchar_t* end = nullptr;
    const auto value = _wcstoui64(text, &end, 10);
    if (!value || !end || *end || value > std::numeric_limits<std::uintptr_t>::max())
        throw std::runtime_error("Invalid inherited handle");
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
}
int Worker(HANDLE inherited_image, HANDLE inherited_response) {
    Handle image_handle(inherited_image), pipe(inherited_response);
    auto result = common::Result<ocr::Document>::Failure(L"OCR 子进程无法读取截图，请重新截图。");
    try {
        View view{MapViewOfFile(image_handle.Get(), FILE_MAP_READ, 0, 0, 0)};
        if (!view.data) throw std::runtime_error("Cannot map OCR image");
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(view.data, &info, sizeof(info)) || info.RegionSize < sizeof(InputHeader))
            throw std::runtime_error("Invalid OCR mapping");
        const auto header = *static_cast<const InputHeader*>(view.data);
        if (header.magic != kMagic || header.version != kVersion || header.width <= 0 || header.height <= 0 ||
            header.width > 10000 || header.height > 10000) throw std::runtime_error("Invalid OCR image header");
        const auto size = static_cast<std::size_t>(header.width) * header.height * 4;
        if (size > info.RegionSize - sizeof(InputHeader)) throw std::runtime_error("Truncated OCR image");
        std::vector<std::uint8_t> pixels(size);
        std::memcpy(pixels.data(), static_cast<const std::uint8_t*>(view.data) + sizeof(InputHeader), size);
        view.Reset(); image_handle.Reset();
        // All model and inference allocations belong exclusively to this process.
        ocr::OneOcrService service;
        result = service.RecognizeDocument({header.width, header.height, std::move(pixels)});
    } catch (const std::exception&) { /* Return a bounded error packet to the parent. */ }
    Packet packet;
    try { packet = Serialize(result); }
    catch (const std::exception&) {
        result = common::Result<ocr::Document>::Failure(L"识别文字过多，请缩小选区后重试。");
        packet = Serialize(result);
    }
    const ResponseHeader header{kMagic, kVersion, result ? 0u : 1u, static_cast<std::uint32_t>(packet.bytes.size())};
    return WriteAll(pipe.Get(), &header, sizeof(header)) && WriteAll(pipe.Get(), packet.bytes.data(), packet.bytes.size()) ? 0 : 2;
}
} // namespace

namespace ocr {
common::Result<Document> RecognizeInWorker(capture::CapturedImage image, std::stop_token stop,
                                          const std::filesystem::path& executable) {
    if (image.IsEmpty()) return common::Result<Document>::Failure(L"请先选择需要识别的截图区域。");
    if (image.Width() > 10000 || image.Height() > 10000)
        return common::Result<Document>::Failure(L"识别区域过大，请选择宽高不超过 10000 像素的区域。");
    const auto& pixels = static_cast<const capture::CapturedImage&>(image).Pixels();
    if (pixels.size() != image.RowStride() * static_cast<std::size_t>(image.Height()))
        return common::Result<Document>::Failure(L"截图像素数据不完整，请重新截图。");
    if (stop.stop_requested()) return common::Result<Document>::Failure(L"已取消文字识别。");
    Handle job(CreateJobObjectW(nullptr, nullptr));
    Handle process;
    try {
        if (!job.Get()) throw std::runtime_error("Cannot create OCR job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limit, sizeof(limit)))
            throw std::runtime_error("Cannot configure OCR job");
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        const auto size = sizeof(InputHeader) + pixels.size();
        Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE,
            static_cast<DWORD>(static_cast<std::uint64_t>(size) >> 32), static_cast<DWORD>(size), nullptr));
        if (!mapping.Get()) throw std::runtime_error("Cannot allocate OCR mapping");
        View view{MapViewOfFile(mapping.Get(), FILE_MAP_WRITE, 0, 0, size)};
        if (!view.data) throw std::runtime_error("Cannot write OCR image");
        const InputHeader input{kMagic, kVersion, image.Width(), image.Height()};
        std::memcpy(view.data, &input, sizeof(input));
        std::memcpy(static_cast<std::uint8_t*>(view.data) + sizeof(input), pixels.data(), pixels.size());
        image = {}; // Do not retain the cropped pixel allocation while waiting.
        view.Reset();
        HANDLE read = nullptr, write = nullptr;
        if (!CreatePipe(&read, &write, &security, 65536)) throw std::runtime_error("Cannot create OCR result pipe");
        Handle reader(read), writer(write);
        if (!SetHandleInformation(reader.Get(), HANDLE_FLAG_INHERIT, 0)) throw std::runtime_error("Cannot protect OCR read handle");
        Attributes attributes;
        std::array<HANDLE, 2> inherited{mapping.Get(), writer.Get()};
        if (!UpdateProcThreadAttribute(attributes.list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited.data(), sizeof(inherited), nullptr, nullptr)) throw std::runtime_error("Cannot restrict OCR inherited handles");
        const auto path = executable.empty() ? CurrentExecutable() : executable;
        std::wstring command = L"\"" + path.wstring() + L"\" --ocr-worker " +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(mapping.Get())) + L" " +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(writer.Get()));
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startup.StartupInfo.wShowWindow = SW_HIDE;
        startup.lpAttributeList = attributes.list;
        PROCESS_INFORMATION launched{};
        if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
            nullptr, nullptr, &startup.StartupInfo, &launched)) throw std::runtime_error("Cannot start OCR worker");
        process.Reset(launched.hProcess);
        Handle thread(launched.hThread);
        if (!AssignProcessToJobObject(job.Get(), process.Get())) {
            TerminateProcess(process.Get(), 2);
            WaitForSingleObject(process.Get(), 5000);
            throw std::runtime_error("Cannot attach OCR worker to job");
        }
        std::stop_callback cancel(stop, [&] { TerminateJobObject(job.Get(), 3); });
        if (ResumeThread(thread.Get()) == static_cast<DWORD>(-1)) throw std::runtime_error("Cannot resume OCR worker");
        writer.Reset(); mapping.Reset();
        const auto deadline = std::chrono::steady_clock::now() + kTimeout;
        ResponseHeader header{};
        ReadAll(reader.Get(), process.Get(), &header, sizeof(header), stop, deadline);
        if (header.magic != kMagic || header.version != kVersion || header.failed > 1 || header.size > kMaxResponse)
            throw std::runtime_error("Invalid OCR response header");
        std::vector<std::uint8_t> response(header.size);
        ReadAll(reader.Get(), process.Get(), response.data(), response.size(), stop, deadline);
        auto result = Deserialize(header, response);
        while (WaitForSingleObject(process.Get(), 25) == WAIT_TIMEOUT) {
            if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("OCR worker shutdown interrupted");
        }
        DWORD exit_code = 0;
        if (!GetExitCodeProcess(process.Get(), &exit_code) || exit_code != 0)
            throw std::runtime_error("OCR worker failed");
        if (stop.stop_requested()) return common::Result<Document>::Failure(L"已取消文字识别。");
        return result; // The worker has exited before its result reaches the UI.
    } catch (const std::exception&) {
        if (job.Get()) TerminateJobObject(job.Get(), 2);
        if (process.Get()) WaitForSingleObject(process.Get(), 5000);
        return common::Result<Document>::Failure(stop.stop_requested() ? L"已取消文字识别。" :
            L"OCR 子进程未能完成识别，请重新选择区域后重试。");
    }
}

std::optional<int> RunWorkerIfRequested() {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return std::nullopt;
    const bool worker = count > 1 && wcscmp(arguments[1], L"--ocr-worker") == 0;
    if (!worker) { LocalFree(arguments); return std::nullopt; }
    int code = 2;
    try {
        if (count == 4) code = Worker(ParseHandle(arguments[2]), ParseHandle(arguments[3]));
    } catch (const std::exception&) { /* Invalid worker invocations never initialize the GUI. */ }
    LocalFree(arguments);
    return code;
}
} // namespace ocr
