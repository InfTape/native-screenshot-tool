#include "capture/GifRecordingWriter.h"

#include <algorithm>
#include <cstring>

namespace {
using Microsoft::WRL::ComPtr;

void Check(HRESULT result) {
    if (FAILED(result)) throw result;
}

void SetShort(IWICMetadataQueryWriter* metadata, const wchar_t* key, USHORT value) {
    PROPVARIANT property{};
    property.vt = VT_UI2;
    property.uiVal = value;
    Check(metadata->SetMetadataByName(key, &property));
}
}  // namespace

namespace capture {

void GifRecordingWriter::Initialize(IStream* stream, UINT width, UINT height) {
    if (!stream || !width || !height || width > 65535 || height > 65535 ||
        static_cast<ULONGLONG>(width) * height * 4 > MAXUINT) throw E_INVALIDARG;
    width_ = width;
    height_ = height;
    pending_.clear();
    previous_.clear();
    pending_time_ = emitted_centiseconds_ = 0;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&factory_)));
    Check(factory_->CreateEncoder(GUID_ContainerFormatGif, nullptr, &encoder_));
    Check(encoder_->Initialize(stream, WICBitmapEncoderNoCache));

    ComPtr<IWICMetadataQueryWriter> metadata;
    Check(encoder_->GetMetadataQueryWriter(&metadata));
    SetShort(metadata.Get(), L"/logscrdesc/Width", static_cast<USHORT>(width));
    SetShort(metadata.Get(), L"/logscrdesc/Height", static_cast<USHORT>(height));
    BYTE application[] = {'N','E','T','S','C','A','P','E','2','.','0'};
    // WIC application data includes the sub-block length, then loop ID and count.
    BYTE loop[] = {3, 1, 0, 0};
    PROPVARIANT property{};
    property.vt = VT_UI1 | VT_VECTOR;
    property.caub = {static_cast<ULONG>(sizeof(application)), application};
    Check(metadata->SetMetadataByName(L"/appext/Application", &property));
    property.caub = {static_cast<ULONG>(sizeof(loop)), loop};
    Check(metadata->SetMetadataByName(L"/appext/Data", &property));
}

void GifRecordingWriter::AddFrame(const BYTE* bottom_up_bgr, LONGLONG timestamp) {
    if (!encoder_ || !bottom_up_bgr || timestamp < pending_time_) throw E_INVALIDARG;
    const size_t stride = static_cast<size_t>(width_) * 4;
    std::vector<BYTE> pixels(stride * height_);
    for (UINT y = 0; y < height_; ++y) {
        BYTE* row = pixels.data() + y * stride;
        std::memcpy(row, bottom_up_bgr + (height_ - 1 - y) * stride, stride);
        // GDI's unused alpha channel can vary without any visible change.
        for (UINT x = 0; x < width_; ++x) row[x * 4 + 3] = 255;
    }
    if (pending_.empty()) {
        pending_ = std::move(pixels);
        pending_time_ = timestamp;
        return;
    }
    if (pixels == pending_ && timestamp - pending_time_ < 650LL * 10'000'000) return;
    WritePending(timestamp);
    previous_ = std::move(pending_);
    pending_ = std::move(pixels);
    pending_time_ = timestamp;
}

RECT GifRecordingWriter::ChangedBounds() const {
    if (previous_.empty()) return {0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    RECT bounds{static_cast<LONG>(width_), static_cast<LONG>(height_), 0, 0};
    for (UINT y = 0; y < height_; ++y) {
        for (UINT x = 0; x < width_; ++x) {
            const size_t offset = (static_cast<size_t>(y) * width_ + x) * 4;
            if (std::memcmp(pending_.data() + offset, previous_.data() + offset, 3) == 0) continue;
            bounds.left = std::min(bounds.left, static_cast<LONG>(x));
            bounds.top = std::min(bounds.top, static_cast<LONG>(y));
            bounds.right = std::max(bounds.right, static_cast<LONG>(x + 1));
            bounds.bottom = std::max(bounds.bottom, static_cast<LONG>(y + 1));
        }
    }
    // A long static hold may need another delay block; redraw one opaque pixel.
    if (bounds.right <= bounds.left) return {0, 0, 1, 1};
    return bounds;
}

void GifRecordingWriter::WritePending(LONGLONG timestamp) {
    const RECT bounds = ChangedBounds();
    const UINT width = bounds.right - bounds.left;
    const UINT height = bounds.bottom - bounds.top;
    std::vector<BYTE> cropped(static_cast<size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(cropped.data() + static_cast<size_t>(y) * width * 4,
                    pending_.data() + (static_cast<size_t>(y + bounds.top) * width_ + bounds.left) * 4,
                    static_cast<size_t>(width) * 4);
    }
    ComPtr<IWICBitmap> bitmap;
    Check(factory_->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGR,
        width * 4, static_cast<UINT>(cropped.size()), cropped.data(), &bitmap));
    ComPtr<IWICPalette> palette;
    Check(factory_->CreatePalette(&palette));
    Check(palette->InitializeFromBitmap(bitmap.Get(), 256, FALSE));
    ComPtr<IWICFormatConverter> indexed;
    Check(factory_->CreateFormatConverter(&indexed));
    // No spatial dithering keeps small text and thin lines stable and crisp.
    Check(indexed->Initialize(bitmap.Get(), GUID_WICPixelFormat8bppIndexed,
        WICBitmapDitherTypeNone, palette.Get(), 0, WICBitmapPaletteTypeCustom));

    const LONGLONG target_centiseconds = (timestamp + 50'000) / 100'000;
    LONGLONG delay = std::max<LONGLONG>(2, target_centiseconds - emitted_centiseconds_);
    while (delay > 0) {
        USHORT part = static_cast<USHORT>(std::min<LONGLONG>(65535, delay));
        if (delay - part == 1) --part;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> options;
        Check(encoder_->CreateNewFrame(&frame, &options));
        Check(frame->Initialize(options.Get()));
        Check(frame->SetSize(width, height));
        WICPixelFormatGUID format = GUID_WICPixelFormat8bppIndexed;
        Check(frame->SetPixelFormat(&format));
        if (format != GUID_WICPixelFormat8bppIndexed) throw WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        Check(frame->SetPalette(palette.Get()));
        ComPtr<IWICMetadataQueryWriter> metadata;
        Check(frame->GetMetadataQueryWriter(&metadata));
        SetShort(metadata.Get(), L"/imgdesc/Left", static_cast<USHORT>(bounds.left));
        SetShort(metadata.Get(), L"/imgdesc/Top", static_cast<USHORT>(bounds.top));
        SetShort(metadata.Get(), L"/grctlext/Delay", part);
        PROPVARIANT disposal{};
        disposal.vt = VT_UI1;
        disposal.bVal = 1;  // Leave prior pixels in place for cropped changes.
        Check(metadata->SetMetadataByName(L"/grctlext/Disposal", &disposal));
        Check(frame->WriteSource(indexed.Get(), nullptr));
        Check(frame->Commit());
        delay -= part;
        emitted_centiseconds_ += part;
    }
}

void GifRecordingWriter::Finish(LONGLONG timestamp) {
    if (!encoder_ || timestamp < pending_time_) throw E_INVALIDARG;
    if (!pending_.empty()) WritePending(timestamp);
    Check(encoder_->Commit());
    encoder_.Reset();
    pending_.clear();
    previous_.clear();
}

}  // namespace capture
