#pragma once

#include <Windows.h>

#include <memory>
#include <vector>

namespace capture {

enum class WgcRowOrder { TopDown, BottomUp };

// Worker-thread-only WGC capture, with native-pixel D3D11 cropping/composition.
// Shared by screenshots and recordings. No GDI capture or fallback.
class WgcCaptureSource {
public:
    WgcCaptureSource();
    ~WgcCaptureSource();
    WgcCaptureSource(const WgcCaptureSource&) = delete;
    WgcCaptureSource& operator=(const WgcCaptureSource&) = delete;

    void Initialize(const RECT& region, HWND window, UINT output_width, UINT output_height,
                    bool include_cursor = true);
    void UpdateRegion(const RECT& region);
    // False means the window closed, was hidden/minimized, or changed size.
    bool ReadFrame(std::vector<BYTE>& pixels, WgcRowOrder row_order = WgcRowOrder::BottomUp);

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}  // namespace capture
