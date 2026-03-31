#include "ui/RegionSelectionBuffers.h"

#include <Windows.h>

#include "common/RectUtils.h"
#include "common/Win32Error.h"
#include "ui/OverlayPrimitives.h"

namespace ui {

RegionSelectionBuffers::~RegionSelectionBuffers() {
    Destroy();
}

common::Result<void> RegionSelectionBuffers::Initialize(
    HWND window,
    const SIZE& buffer_size,
    BYTE overlay_alpha,
    const PaintBaseCallback& paint_base,
    const DrawInstructionsCallback& draw_instructions) {
    Destroy();

    if (window == nullptr || buffer_size.cx <= 0 || buffer_size.cy <= 0) {
        return common::Result<void>::Failure(L"初始化选区缓冲失败。");
    }

    overlay_alpha_ = overlay_alpha;
    buffer_size_ = buffer_size;

    common::WindowDcHandle window_dc(window, GetDC(window));
    if (!window_dc) {
        const std::wstring error_message =
            L"获取选区窗口 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        Destroy();
        return common::Result<void>::Failure(error_message);
    }

    auto base_created = CreateBaseBuffer(window_dc.Get(), paint_base);
    auto dimmed_created =
        base_created ? CreateDimmedBuffer(window_dc.Get(), draw_instructions) : base_created;
    auto back_created = dimmed_created ? CreateBackBuffer(window_dc.Get()) : dimmed_created;

    if (!back_created) {
        Destroy();
        return common::Result<void>::Failure(back_created.Error());
    }

    const RECT full_rect{0, 0, buffer_size_.cx, buffer_size_.cy};
    UpdateBackBuffer(full_rect, nullptr, PaintOverlayCallback{});
    return common::Result<void>::Success();
}

void RegionSelectionBuffers::Destroy() {
    DestroyBackBuffer();
    DestroyDimmedBuffer();
    DestroyBaseBuffer();
    overlay_alpha_ = 0;
    buffer_size_ = SIZE{};
}

void RegionSelectionBuffers::RebuildSourceBuffers(const PaintBaseCallback& paint_base,
                                                  const DrawInstructionsCallback& draw_instructions) {
    RenderBaseBuffer(paint_base);
    RenderDimmedBuffer(draw_instructions);
}

void RegionSelectionBuffers::UpdateBackBuffer(const RECT& dirty_rect,
                                              const RECT* revealed_rect,
                                              const PaintOverlayCallback& paint_overlay) {
    if (!back_buffer_.dc || !base_buffer_.dc || !dimmed_buffer_.dc) {
        return;
    }

    const RECT clamped_dirty_rect = ClampRectToBuffer(dirty_rect);
    if (!common::HasArea(clamped_dirty_rect)) {
        return;
    }

    BitBlt(back_buffer_.dc.Get(),
           clamped_dirty_rect.left,
           clamped_dirty_rect.top,
           common::RectWidth(clamped_dirty_rect),
           common::RectHeight(clamped_dirty_rect),
           dimmed_buffer_.dc.Get(),
           clamped_dirty_rect.left,
           clamped_dirty_rect.top,
           SRCCOPY);

    if (revealed_rect != nullptr && common::HasArea(*revealed_rect)) {
        RECT visible_region{};
        if (IntersectRect(&visible_region, revealed_rect, &clamped_dirty_rect)) {
            BitBlt(back_buffer_.dc.Get(),
                   visible_region.left,
                   visible_region.top,
                   common::RectWidth(visible_region),
                   common::RectHeight(visible_region),
                   base_buffer_.dc.Get(),
                   visible_region.left,
                   visible_region.top,
                   SRCCOPY);
        }
    }

    if (paint_overlay) {
        const RECT client_rect{0, 0, buffer_size_.cx, buffer_size_.cy};
        const int saved_dc = SaveDC(back_buffer_.dc.Get());
        if (saved_dc != 0) {
            IntersectClipRect(back_buffer_.dc.Get(),
                              clamped_dirty_rect.left,
                              clamped_dirty_rect.top,
                              clamped_dirty_rect.right,
                              clamped_dirty_rect.bottom);
        }

        paint_overlay(back_buffer_.dc.Get(), client_rect);

        if (saved_dc != 0) {
            RestoreDC(back_buffer_.dc.Get(), saved_dc);
        }
    }
}

void RegionSelectionBuffers::PaintFrame(HDC hdc,
                                        const RECT& client_rect,
                                        const RECT* revealed_rect,
                                        const PaintBaseCallback& paint_base,
                                        const DrawInstructionsCallback& draw_instructions,
                                        const PaintOverlayCallback& paint_overlay) const {
    if (dimmed_buffer_.dc) {
        BitBlt(hdc,
               0,
               0,
               common::RectWidth(client_rect),
               common::RectHeight(client_rect),
               dimmed_buffer_.dc.Get(),
               0,
               0,
               SRCCOPY);
    } else {
        if (paint_base) {
            paint_base(hdc);
        }
        AlphaFillRect(hdc, client_rect, overlay_alpha_);
        if (draw_instructions) {
            draw_instructions(hdc, client_rect);
        }
    }

    if (revealed_rect != nullptr && common::HasArea(*revealed_rect) && base_buffer_.dc) {
        BitBlt(hdc,
               revealed_rect->left,
               revealed_rect->top,
               common::RectWidth(*revealed_rect),
               common::RectHeight(*revealed_rect),
               base_buffer_.dc.Get(),
               revealed_rect->left,
               revealed_rect->top,
               SRCCOPY);
    }

    if (paint_overlay) {
        paint_overlay(hdc, client_rect);
    }
}

bool RegionSelectionBuffers::HasBackBuffer() const {
    return static_cast<bool>(back_buffer_.dc);
}

HDC RegionSelectionBuffers::BackBufferDc() const {
    return back_buffer_.dc.Get();
}

SIZE RegionSelectionBuffers::BufferSize() const {
    return buffer_size_;
}

common::Result<void> RegionSelectionBuffers::CreateBaseBuffer(HDC reference_dc,
                                                              const PaintBaseCallback& paint_base) {
    base_buffer_.dc.Reset(CreateCompatibleDC(reference_dc));
    if (!base_buffer_.dc) {
        return common::Result<void>::Failure(
            L"创建基础底图 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    base_buffer_.bitmap.Reset(CreateCompatibleBitmap(reference_dc, buffer_size_.cx, buffer_size_.cy));
    if (!base_buffer_.bitmap) {
        const std::wstring error_message =
            L"创建基础底图位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyBaseBuffer();
        return common::Result<void>::Failure(error_message);
    }

    if (!base_buffer_.selected_bitmap.Select(base_buffer_.dc.Get(), base_buffer_.bitmap.Get())) {
        const std::wstring error_message =
            L"绑定基础底图位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyBaseBuffer();
        return common::Result<void>::Failure(error_message);
    }

    RenderBaseBuffer(paint_base);
    return common::Result<void>::Success();
}

void RegionSelectionBuffers::DestroyBaseBuffer() {
    base_buffer_.selected_bitmap.Reset();
    base_buffer_.bitmap.Reset();
    base_buffer_.dc.Reset();
}

common::Result<void> RegionSelectionBuffers::CreateDimmedBuffer(
    HDC reference_dc,
    const DrawInstructionsCallback& draw_instructions) {
    dimmed_buffer_.dc.Reset(CreateCompatibleDC(reference_dc));
    if (!dimmed_buffer_.dc) {
        return common::Result<void>::Failure(
            L"创建暗化底图 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    dimmed_buffer_.bitmap.Reset(
        CreateCompatibleBitmap(reference_dc, buffer_size_.cx, buffer_size_.cy));
    if (!dimmed_buffer_.bitmap) {
        const std::wstring error_message =
            L"创建暗化底图位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyDimmedBuffer();
        return common::Result<void>::Failure(error_message);
    }

    if (!dimmed_buffer_.selected_bitmap.Select(dimmed_buffer_.dc.Get(), dimmed_buffer_.bitmap.Get())) {
        const std::wstring error_message =
            L"绑定暗化底图位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyDimmedBuffer();
        return common::Result<void>::Failure(error_message);
    }

    RenderDimmedBuffer(draw_instructions);
    return common::Result<void>::Success();
}

void RegionSelectionBuffers::DestroyDimmedBuffer() {
    dimmed_buffer_.selected_bitmap.Reset();
    dimmed_buffer_.bitmap.Reset();
    dimmed_buffer_.dc.Reset();
}

common::Result<void> RegionSelectionBuffers::CreateBackBuffer(HDC reference_dc) {
    back_buffer_.dc.Reset(CreateCompatibleDC(reference_dc));
    if (!back_buffer_.dc) {
        return common::Result<void>::Failure(
            L"创建选区后台缓冲 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    back_buffer_.bitmap.Reset(CreateCompatibleBitmap(reference_dc, buffer_size_.cx, buffer_size_.cy));
    if (!back_buffer_.bitmap) {
        const std::wstring error_message =
            L"创建选区后台缓冲位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyBackBuffer();
        return common::Result<void>::Failure(error_message);
    }

    if (!back_buffer_.selected_bitmap.Select(back_buffer_.dc.Get(), back_buffer_.bitmap.Get())) {
        const std::wstring error_message =
            L"绑定选区后台缓冲位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyBackBuffer();
        return common::Result<void>::Failure(error_message);
    }

    return common::Result<void>::Success();
}

void RegionSelectionBuffers::DestroyBackBuffer() {
    back_buffer_.selected_bitmap.Reset();
    back_buffer_.bitmap.Reset();
    back_buffer_.dc.Reset();
}

void RegionSelectionBuffers::RenderBaseBuffer(const PaintBaseCallback& paint_base) {
    if (base_buffer_.dc && paint_base) {
        paint_base(base_buffer_.dc.Get());
    }
}

void RegionSelectionBuffers::RenderDimmedBuffer(const DrawInstructionsCallback& draw_instructions) {
    if (!dimmed_buffer_.dc || !base_buffer_.dc) {
        return;
    }

    BitBlt(dimmed_buffer_.dc.Get(),
           0,
           0,
           buffer_size_.cx,
           buffer_size_.cy,
           base_buffer_.dc.Get(),
           0,
           0,
           SRCCOPY);

    const RECT full_rect{0, 0, buffer_size_.cx, buffer_size_.cy};
    AlphaFillRect(dimmed_buffer_.dc.Get(), full_rect, overlay_alpha_);
    if (draw_instructions) {
        draw_instructions(dimmed_buffer_.dc.Get(), full_rect);
    }
}

RECT RegionSelectionBuffers::ClampRectToBuffer(const RECT& rect) const {
    return common::ClampRectToBounds(rect, buffer_size_.cx, buffer_size_.cy);
}

}  // namespace ui
