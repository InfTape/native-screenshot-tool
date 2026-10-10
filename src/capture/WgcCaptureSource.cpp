#include "capture/WgcCaptureSource.h"

#include <d3d11.h>
#include <dxgi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace capture {
namespace {

using namespace winrt::Windows::Graphics::Capture;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using winrt::Windows::Foundation::Metadata::ApiInformation;

struct MonitorRegion {
    HMONITOR monitor = nullptr;
    RECT bounds{};
};

BOOL CALLBACK CollectMonitors(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    MONITORINFO info{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor, &info)) return FALSE;
    reinterpret_cast<std::vector<MonitorRegion>*>(context)->push_back({monitor, info.rcMonitor});
    return TRUE;
}

}  // namespace

struct WgcCaptureSource::Implementation {
    struct Stream {
        GraphicsCaptureItem item{nullptr};
        Direct3D11CaptureFramePool pool{nullptr};
        GraphicsCaptureSession session{nullptr};
        winrt::com_ptr<ID3D11Texture2D> latest;
        RECT bounds{};
        HMONITOR monitor = nullptr;
        RECT crop{};
        UINT destination_x = 0;
        UINT destination_y = 0;
        std::shared_ptr<std::atomic<bool>> closed = std::make_shared<std::atomic<bool>>(false);
        winrt::event_token closed_token{};
        bool has_frame = false;

        ~Stream() {
            // Close while the worker's WinRT apartment and D3D device are alive.
            try { if (item && closed_token.value) item.Closed(closed_token); } catch (...) {}
            try { if (session) session.Close(); } catch (...) {}
            try { if (pool) pool.Close(); } catch (...) {}
        }
    };

    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    IDirect3DDevice capture_device{nullptr};
    winrt::com_ptr<ID3D11Texture2D> composed;
    winrt::com_ptr<ID3D11RenderTargetView> render_target;
    winrt::com_ptr<ID3D11Texture2D> staging;
    std::vector<std::unique_ptr<Stream>> streams;
    HWND window = nullptr;
    UINT width = 0;
    UINT height = 0;
    bool include_cursor = true;

    void AddStream(GraphicsCaptureItem item, const RECT& bounds, const RECT& intersection,
                   const RECT& output_region, HMONITOR monitor = nullptr) {
        auto stream = std::make_unique<Stream>();
        stream->item = item;
        stream->bounds = bounds;
        stream->monitor = monitor;
        stream->crop = {intersection.left - bounds.left, intersection.top - bounds.top,
                        intersection.right - bounds.left, intersection.bottom - bounds.top};
        stream->destination_x = static_cast<UINT>(intersection.left - output_region.left);
        stream->destination_y = static_cast<UINT>(intersection.top - output_region.top);
        const auto size = item.Size();
        if (size.Width != bounds.right - bounds.left || size.Height != bounds.bottom - bounds.top) {
            throw winrt::hresult_error(E_FAIL, L"WGC 画面尺寸与录制范围不一致，请重新选择。");
        }
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = static_cast<UINT>(size.Width);
        texture.Height = static_cast<UINT>(size.Height);
        texture.MipLevels = 1;
        texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texture.SampleDesc.Count = 1;
        texture.Usage = D3D11_USAGE_DEFAULT;
        winrt::check_hresult(device->CreateTexture2D(&texture, nullptr, stream->latest.put()));
        stream->pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            capture_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        stream->session = stream->pool.CreateCaptureSession(item);
        if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession",
                                               L"IsCursorCaptureEnabled")) {
            stream->session.IsCursorCaptureEnabled(include_cursor);
        } else if (!include_cursor) {
            throw winrt::hresult_error(E_NOTIMPL, L"不含鼠标指针的截图需要 Windows 10 2004 或更新版本。");
        }
        // A shared flag keeps the close callback safe even during session teardown.
        stream->closed_token = item.Closed([closed = stream->closed](auto const&, auto const&) {
            closed->store(true);
        });
        stream->session.StartCapture();
        streams.push_back(std::move(stream));
    }

    bool Update(Stream& stream) {
        if (stream.monitor) {
            MONITORINFO info{sizeof(MONITORINFO)};
            if (!GetMonitorInfoW(stream.monitor, &info) || !EqualRect(&info.rcMonitor, &stream.bounds)) {
                throw winrt::hresult_error(E_FAIL, L"显示器布局或分辨率已改变，请重新开始录制。");
            }
        }
        if (stream.closed->load()) {
            if (window) return false;
            throw winrt::hresult_error(E_FAIL, L"录制的显示器已断开，请重新选择录制范围。");
        }
        // Bound draining so a fast producer cannot starve stop/encoding.
        for (int index = 0; index < 3; ++index) {
            auto frame = stream.pool.TryGetNextFrame();
            if (!frame) break;
            const auto size = frame.ContentSize();
            if (size.Width != stream.bounds.right - stream.bounds.left ||
                size.Height != stream.bounds.bottom - stream.bounds.top) {
                frame.Close();
                if (window) return false;
                throw winrt::hresult_error(E_FAIL, L"显示器分辨率已改变，请重新开始录制。");
            }
            auto access = frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            winrt::com_ptr<ID3D11Texture2D> texture;
            winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            if (description.Width < static_cast<UINT>(size.Width) ||
                description.Height < static_cast<UINT>(size.Height)) {
                throw winrt::hresult_error(E_FAIL, L"WGC 返回了不完整的画面。");
            }
            D3D11_BOX content{0, 0, 0, static_cast<UINT>(size.Width), static_cast<UINT>(size.Height), 1};
            context->CopySubresourceRegion(stream.latest.get(), 0, 0, 0, 0, texture.get(), 0, &content);
            frame.Close();
            stream.has_frame = true;
        }
        return true;
    }
};

WgcCaptureSource::WgcCaptureSource() : implementation_(std::make_unique<Implementation>()) {}
WgcCaptureSource::~WgcCaptureSource() = default;

void WgcCaptureSource::Initialize(const RECT& region, HWND window, UINT width, UINT height, bool include_cursor) {
    if (!GraphicsCaptureSession::IsSupported()) {
        throw winrt::hresult_error(E_NOTIMPL, L"当前系统不支持 WGC 采集，请使用 Windows 10 1903 或更新版本。");
    }
    auto& state = *implementation_;
    state.window = window;
    state.width = width;
    state.height = height;
    state.include_cursor = include_cursor;
    winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        state.device.put(), nullptr, state.context.put()));
    auto dxgi = state.device.as<IDXGIDevice>();
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
    state.capture_device = inspectable.as<IDirect3DDevice>();

    D3D11_TEXTURE2D_DESC output{};
    output.Width = width;
    output.Height = height;
    output.MipLevels = 1;
    output.ArraySize = 1;
    output.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    output.SampleDesc.Count = 1;
    output.Usage = D3D11_USAGE_DEFAULT;
    output.BindFlags = D3D11_BIND_RENDER_TARGET;
    winrt::check_hresult(state.device->CreateTexture2D(&output, nullptr, state.composed.put()));
    winrt::check_hresult(state.device->CreateRenderTargetView(state.composed.get(), nullptr, state.render_target.put()));
    output.Usage = D3D11_USAGE_STAGING;
    output.BindFlags = 0;
    output.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    winrt::check_hresult(state.device->CreateTexture2D(&output, nullptr, state.staging.put()));
    auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    if (window) {
        GraphicsCaptureItem item{nullptr};
        winrt::check_hresult(factory->CreateForWindow(window, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
        state.AddStream(item, region, region, region);
    } else {
        std::vector<MonitorRegion> monitors;
        if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitors, reinterpret_cast<LPARAM>(&monitors))) {
            winrt::throw_last_error();
        }
        for (const auto& monitor : monitors) {
            RECT intersection{};
            if (!IntersectRect(&intersection, &region, &monitor.bounds)) continue;
            GraphicsCaptureItem item{nullptr};
            winrt::check_hresult(factory->CreateForMonitor(monitor.monitor, winrt::guid_of<GraphicsCaptureItem>(),
                                                          winrt::put_abi(item)));
            state.AddStream(item, monitor.bounds, intersection, region, monitor.monitor);
        }
        if (state.streams.empty()) {
            throw winrt::hresult_error(E_INVALIDARG, L"录制范围不在任何显示器内。");
        }
    }
}

bool WgcCaptureSource::ReadFrame(std::vector<BYTE>& pixels, WgcRowOrder row_order) {
    auto& state = *implementation_;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        if (state.window && (!IsWindow(state.window) || !IsWindowVisible(state.window) || IsIconic(state.window))) {
            return false;
        }
        bool ready = true;
        for (auto& stream : state.streams) {
            if (!state.Update(*stream)) return false;
            ready = ready && stream->has_frame;
        }
        if (ready) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"WGC 未能获取画面，请检查采集目标和显卡驱动。");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Native-pixel copies preserve text detail. Gaps between monitors and odd-size
    // H.264 padding stay black; no scaling or stale pixels enter the output.
    constexpr float black[]{0, 0, 0, 1};
    state.context->ClearRenderTargetView(state.render_target.get(), black);
    for (const auto& stream : state.streams) {
        const RECT& crop = stream->crop;
        D3D11_BOX box{static_cast<UINT>(crop.left), static_cast<UINT>(crop.top), 0,
                      static_cast<UINT>(crop.right), static_cast<UINT>(crop.bottom), 1};
        state.context->CopySubresourceRegion(state.composed.get(), 0, stream->destination_x,
            stream->destination_y, 0, stream->latest.get(), 0, &box);
    }
    state.context->CopyResource(state.staging.get(), state.composed.get());
    pixels.resize(static_cast<size_t>(state.width) * state.height * 4);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    winrt::check_hresult(state.context->Map(state.staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
    const size_t row_bytes = static_cast<size_t>(state.width) * 4;
    for (UINT row = 0; row < state.height; ++row) {
        const UINT output_row = row_order == WgcRowOrder::TopDown ? row : state.height - 1 - row;
        std::memcpy(pixels.data() + static_cast<size_t>(output_row) * row_bytes,
            static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch, row_bytes);
    }
    state.context->Unmap(state.staging.get(), 0);
    winrt::check_hresult(state.device->GetDeviceRemovedReason());
    return true;
}

}  // namespace capture
