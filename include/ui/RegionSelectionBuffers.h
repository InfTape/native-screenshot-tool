#pragma once

#include <Windows.h>

#include <functional>
#include <string>

#include "common/GdiResources.h"
#include "common/Result.h"

namespace ui {

class RegionSelectionBuffers {
public:
    using PaintBaseCallback = std::function<void(HDC hdc)>;
    using PaintOverlayCallback = std::function<void(HDC hdc, const RECT& client_rect)>;
    using DrawInstructionsCallback = std::function<void(HDC hdc, const RECT& bounds)>;

    RegionSelectionBuffers() = default;
    RegionSelectionBuffers(const RegionSelectionBuffers&) = delete;
    RegionSelectionBuffers& operator=(const RegionSelectionBuffers&) = delete;

    ~RegionSelectionBuffers();

    common::Result<void> Initialize(HWND window,
                                    const SIZE& buffer_size,
                                    BYTE overlay_alpha,
                                    const PaintBaseCallback& paint_base,
                                    const DrawInstructionsCallback& draw_instructions);
    void Destroy();

    void RebuildSourceBuffers(const PaintBaseCallback& paint_base,
                              const DrawInstructionsCallback& draw_instructions);
    void UpdateBackBuffer(const RECT& dirty_rect,
                          const RECT* revealed_rect,
                          const PaintOverlayCallback& paint_overlay);
    void PaintFrame(HDC hdc,
                    const RECT& client_rect,
                    const RECT* revealed_rect,
                    const PaintBaseCallback& paint_base,
                    const DrawInstructionsCallback& draw_instructions,
                    const PaintOverlayCallback& paint_overlay) const;

    [[nodiscard]] bool HasBackBuffer() const;
    [[nodiscard]] HDC BackBufferDc() const;
    [[nodiscard]] SIZE BufferSize() const;

private:
    struct BufferSurface {
        common::UniqueDc dc;
        common::UniqueBitmap bitmap;
        common::ScopedSelectObject selected_bitmap;
    };

    common::Result<void> CreateBaseBuffer(HDC reference_dc, const PaintBaseCallback& paint_base);
    void DestroyBaseBuffer();
    common::Result<void> CreateDimmedBuffer(HDC reference_dc,
                                            const DrawInstructionsCallback& draw_instructions);
    void DestroyDimmedBuffer();
    common::Result<void> CreateBackBuffer(HDC reference_dc);
    void DestroyBackBuffer();
    void RenderBaseBuffer(const PaintBaseCallback& paint_base);
    void RenderDimmedBuffer(const DrawInstructionsCallback& draw_instructions);
    RECT ClampRectToBuffer(const RECT& rect) const;

    BYTE overlay_alpha_ = 0;
    BufferSurface base_buffer_;
    BufferSurface dimmed_buffer_;
    BufferSurface back_buffer_;
    SIZE buffer_size_{};
};

}  // namespace ui
