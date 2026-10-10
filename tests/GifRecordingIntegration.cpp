#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "capture/GifRecordingWriter.h"

using Microsoft::WRL::ComPtr;
constexpr UINT kWidth = 241;
constexpr UINT kHeight = 161;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

UINT ReadNumber(IWICMetadataQueryReader* reader, const wchar_t* key) {
    PROPVARIANT value{};
    Require(SUCCEEDED(reader->GetMetadataByName(key, &value)), "read GIF metadata");
    Require(value.vt == VT_UI2 || value.vt == VT_UI1, "GIF numeric metadata type");
    const UINT number = value.vt == VT_UI2 ? value.uiVal : value.bVal;
    PropVariantClear(&value);
    return number;
}

std::vector<BYTE> Source() {
    std::vector<BYTE> pixels(kWidth * kHeight * 4, 255);
    for (UINT y = 0; y < kHeight; ++y) {
        for (UINT x = 0; x < kWidth; ++x) {
            const size_t p = (static_cast<size_t>(kHeight - 1 - y) * kWidth + x) * 4;
            if (x < 10) {  // Color strip detects orientation; fine lines detect resampling.
                pixels[p] = 0;
                pixels[p + 1] = y < kHeight / 2 ? 255 : 0;
                pixels[p + 2] = y < kHeight / 2 ? 0 : 255;
            } else if (x >= 100 && x < 140 && y < 30 && x % 2 == 0) {
                pixels[p] = pixels[p + 1] = pixels[p + 2] = 0;
            }
        }
    }
    return pixels;
}

void PaintChange(std::vector<BYTE>& pixels) {
    for (UINT y = 29; y < 48; ++y) {
        for (UINT x = 17; x < 30; ++x) {
            const size_t p = (static_cast<size_t>(kHeight - 1 - y) * kWidth + x) * 4;
            pixels[p] = 255;
            pixels[p + 1] = pixels[p + 2] = 0;
        }
    }
}

void Validate(IStream* stream, const std::vector<std::vector<BYTE>>& sources,
              const std::vector<UINT>& delays, bool check_crop = false) {
    LARGE_INTEGER zero{};
    Require(SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)), "rewind GIF");
    ComPtr<IWICImagingFactory> factory;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory))), "create GIF decoder factory");
    ComPtr<IWICBitmapDecoder> decoder;
    Require(SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                                       &decoder)), "decode animation");
    UINT frames = 0;
    Require(SUCCEEDED(decoder->GetFrameCount(&frames)) && frames == delays.size(), "merged GIF frame count");
    ComPtr<IWICMetadataQueryReader> metadata;
    Require(SUCCEEDED(decoder->GetMetadataQueryReader(&metadata)), "global GIF metadata");
    Require(ReadNumber(metadata.Get(), L"/logscrdesc/Width") == kWidth &&
            ReadNumber(metadata.Get(), L"/logscrdesc/Height") == kHeight, "native odd GIF dimensions");
    PROPVARIANT loop{};
    Require(SUCCEEDED(metadata->GetMetadataByName(L"/appext/Data", &loop)), "loop extension present");
    Require(loop.vt == (VT_UI1 | VT_VECTOR) && loop.caub.cElems == 4 &&
            loop.caub.pElems[0] == 3 && loop.caub.pElems[1] == 1 &&
            loop.caub.pElems[2] == 0 && loop.caub.pElems[3] == 0, "infinite animation loop");
    PropVariantClear(&loop);
    std::vector<BYTE> composed(kWidth * kHeight * 4);
    for (UINT index = 0; index < frames; ++index) {
        ComPtr<IWICBitmapFrameDecode> frame;
        Require(SUCCEEDED(decoder->GetFrame(index, &frame)), "decode GIF frame");
        metadata.Reset();
        Require(SUCCEEDED(frame->GetMetadataQueryReader(&metadata)), "frame GIF metadata");
        Require(ReadNumber(metadata.Get(), L"/grctlext/Delay") == delays[index], "preserve wall-clock delays");
        Require(ReadNumber(metadata.Get(), L"/grctlext/Disposal") == 1, "retain background between GIF frames");
        const UINT left = ReadNumber(metadata.Get(), L"/imgdesc/Left");
        const UINT top = ReadNumber(metadata.Get(), L"/imgdesc/Top");
        UINT width = 0, height = 0;
        Require(SUCCEEDED(frame->GetSize(&width, &height)), "GIF frame dimensions");
        Require(left + width <= kWidth && top + height <= kHeight, "frame stays inside canvas");
        if (check_crop && index == 1) {
            Require(left == 17 && top == 29 && width == 13 && height == 19, "encode only changed rectangle");
        }
        ComPtr<IWICBitmapSource> rgba;
        Require(SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.Get(), &rgba)), "convert GIF pixels");
        std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
        Require(SUCCEEDED(rgba->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())), "read GIF pixels");
        for (UINT y = 0; y < height; ++y) {
            std::memcpy(composed.data() + (static_cast<size_t>(top + y) * kWidth + left) * 4,
                        pixels.data() + static_cast<size_t>(y) * width * 4, static_cast<size_t>(width) * 4);
        }
        const auto& expected = sources[std::min<size_t>(index, sources.size() - 1)];
        for (UINT y = 0; y < kHeight; ++y) {
            for (UINT x = 0; x < kWidth; ++x) {
                const size_t a = (static_cast<size_t>(y) * kWidth + x) * 4;
                const size_t b = (static_cast<size_t>(kHeight - 1 - y) * kWidth + x) * 4;
                Require(std::memcmp(composed.data() + a, expected.data() + b, 3) == 0,
                        "preserve orientation, colors, and one-pixel lines");
            }
        }
    }
}

void Run() {
    auto source = Source();
    auto changed = source;
    PaintChange(changed);
    ComPtr<IStream> stream;
    Require(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)), "GIF stream");
    capture::GifRecordingWriter writer;
    writer.Initialize(stream.Get(), kWidth, kHeight);
    writer.AddFrame(source.data(), 0);
    writer.AddFrame(source.data(), 2'000'000); // Identical frames extend the first duration.
    writer.AddFrame(changed.data(), 4'000'000);
    writer.AddFrame(source.data(), 5'500'000); // Erasing a prior change must be visible.
    writer.Finish(12'700'000);
    Validate(stream.Get(), {source, changed, source}, {40, 15, 72}, true);

    stream.Reset();
    Require(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)), "static GIF stream");
    writer.Initialize(stream.Get(), kWidth, kHeight);
    writer.AddFrame(source.data(), 0);
    writer.AddFrame(source.data(), 4'000'000);
    writer.Finish(10'000'000);
    Validate(stream.Get(), {source}, {100});

    stream.Reset();
    Require(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)), "long GIF stream");
    writer.Initialize(stream.Get(), kWidth, kHeight);
    writer.AddFrame(source.data(), 0);
    writer.Finish(700LL * 10'000'000);
    Validate(stream.Get(), {source}, {65535, 4465});

    stream.Reset();
    Require(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)), "quick GIF stream");
    writer.Initialize(stream.Get(), kWidth, kHeight);
    writer.AddFrame(source.data(), 0);
    writer.Finish(1);
    Validate(stream.Get(), {source}, {2});
}

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    int result = 0;
    try {
        Run();
        std::cout << "GIF timing, palettes, native pixels, looping, and changed rectangles passed.\n";
    } catch (HRESULT error) {
        std::cerr << "WIC failure: 0x" << std::hex << error << '\n';
        result = 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    CoUninitialize();
    return result;
}
