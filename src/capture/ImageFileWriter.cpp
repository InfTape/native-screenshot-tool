#include "capture/ImageFileWriter.h"

#include <Windows.h>
#include <objbase.h>
#include <wincodec.h>

#include <cstddef>
#include <limits>

#include <wrl/client.h>

#include "common/Win32Error.h"

namespace {

using Microsoft::WRL::ComPtr;

class ScopedComInitialization {
public:
    ScopedComInitialization() {
        result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    }

    ~ScopedComInitialization() {
        if (result_ == S_OK || result_ == S_FALSE) {
            CoUninitialize();
        }
    }

    bool IsAvailable() const {
        return result_ == S_OK || result_ == S_FALSE || result_ == RPC_E_CHANGED_MODE;
    }

    HRESULT Result() const {
        return result_;
    }

private:
    HRESULT result_ = E_FAIL;
};

HRESULT CreateImagingFactory(ComPtr<IWICImagingFactory>& factory) {
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        return hr;
    }

    factory.Reset();
    return CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
}

}  // namespace

namespace capture {

bool ImageFileWriter::Write(const std::wstring& path,
                            const CapturedImage& image,
                            ImageFileFormat format,
                            std::wstring& error_message) const {
    switch (format) {
    case ImageFileFormat::Png:
        return WritePng(path, image, error_message);
    case ImageFileFormat::Bmp:
        return bitmap_writer_.WriteBmp(path, image, error_message);
    }

    error_message = L"不支持的图像保存格式。";
    return false;
}

bool ImageFileWriter::WritePng(const std::wstring& path,
                               const CapturedImage& image,
                               std::wstring& error_message) const {
    if (image.IsEmpty()) {
        error_message = L"当前没有可保存的截图。";
        return false;
    }

    const std::size_t pixel_bytes = image.RowStride() * static_cast<std::size_t>(image.Height());
    if (pixel_bytes > static_cast<std::size_t>(std::numeric_limits<UINT>::max())) {
        error_message = L"图像过大，无法保存为 PNG。";
        return false;
    }

    ScopedComInitialization com_initialization;
    if (!com_initialization.IsAvailable()) {
        error_message =
            L"初始化 PNG 编码组件失败。\n\n" + common::GetLastErrorMessage(com_initialization.Result());
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CreateImagingFactory(factory);
    if (FAILED(hr)) {
        error_message = L"创建 PNG 编码工厂失败。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) {
        error_message = L"创建输出流失败。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        error_message = L"无法打开目标文件进行写入。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) {
        error_message = L"创建 PNG 编码器失败。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        error_message = L"初始化 PNG 编码器失败。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> property_bag;
    hr = encoder->CreateNewFrame(&frame, &property_bag);
    if (FAILED(hr)) {
        error_message = L"创建 PNG 帧失败。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    hr = frame->Initialize(property_bag.Get());
    if (FAILED(hr)) {
        error_message = L"初始化 PNG 帧失败。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    hr = frame->SetSize(static_cast<UINT>(image.Width()), static_cast<UINT>(image.Height()));
    if (FAILED(hr)) {
        error_message = L"设置 PNG 图像尺寸失败。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixel_format);
    if (FAILED(hr)) {
        error_message = L"设置 PNG 像素格式失败。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    if (pixel_format != GUID_WICPixelFormat32bppBGRA) {
        error_message = L"系统 PNG 编码器不支持当前像素格式。";
        return false;
    }

    hr = frame->WritePixels(static_cast<UINT>(image.Height()),
                            static_cast<UINT>(image.RowStride()),
                            static_cast<UINT>(pixel_bytes),
                            const_cast<BYTE*>(image.Pixels().data()));
    if (FAILED(hr)) {
        error_message = L"写入 PNG 像素数据失败。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        error_message = L"提交 PNG 帧失败。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        error_message = L"提交 PNG 文件失败。\n\n" + common::GetLastErrorMessage(hr);
        return false;
    }

    return true;
}

}  // namespace capture
