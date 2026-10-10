#pragma once

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <vector>

namespace capture {

// Worker-thread encoder. Keeps only the current and previous source frames,
// writes changes at native resolution, and merges identical frames by duration.
// COM must already be initialized. Failed WIC calls throw HRESULT to the recorder.
class GifRecordingWriter {
public:
    void Initialize(IStream* stream, UINT width, UINT height);
    void AddFrame(const BYTE* bottom_up_bgr, LONGLONG timestamp);
    void Finish(LONGLONG timestamp);

private:
    void WritePending(LONGLONG timestamp);
    RECT ChangedBounds() const;

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory_;
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder_;
    UINT width_ = 0;
    UINT height_ = 0;
    std::vector<BYTE> pending_;
    std::vector<BYTE> previous_;
    LONGLONG pending_time_ = 0;
    LONGLONG emitted_centiseconds_ = 0;
};

}  // namespace capture
