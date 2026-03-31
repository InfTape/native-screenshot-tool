#include "common/Direct2D.h"

#include <d2d1.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace {

constexpr float kArrowHeadAngleRadians = 0.5235988f;  // 30 degrees

bool IsValidRect(const RECT& rect) {
    return rect.left < rect.right && rect.top < rect.bottom;
}

bool HasClipRect(const RECT* clip_rect) {
    return clip_rect != nullptr && IsValidRect(*clip_rect);
}

std::wstring FormatHresultMessage(const wchar_t* prefix, HRESULT hr) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return std::wstring(prefix) + buffer;
}

D2D1_POINT_2F ToD2DPoint(const POINT& point) {
    return D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));
}

D2D1_RECT_F ToD2DRect(const RECT& rect) {
    return D2D1::RectF(static_cast<float>(rect.left),
                       static_cast<float>(rect.top),
                       static_cast<float>(rect.right),
                       static_cast<float>(rect.bottom));
}

struct DcRenderContext {
    ID2D1Factory* factory = nullptr;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> render_target;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
};

common::Result<ID2D1Factory*> GetDirect2DFactory() {
    static Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    if (factory) {
        return common::Result<ID2D1Factory*>::Success(factory.Get());
    }

    const HRESULT hr =
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return common::Result<ID2D1Factory*>::Failure(
            FormatHresultMessage(L"创建 Direct2D 工厂失败，HRESULT=", hr));
    }

    return common::Result<ID2D1Factory*>::Success(factory.Get());
}

common::Result<DcRenderContext> PrepareDcRenderTarget(HDC hdc,
                                                      const RECT& render_bounds,
                                                      COLORREF color) {
    if (hdc == nullptr) {
        return common::Result<DcRenderContext>::Failure(L"Direct2D 绘制目标无效。");
    }

    if (!IsValidRect(render_bounds)) {
        return common::Result<DcRenderContext>::Failure(L"Direct2D 绘制范围无效。");
    }

    auto factory_result = GetDirect2DFactory();
    if (!factory_result) {
        return common::Result<DcRenderContext>::Failure(factory_result.Error());
    }

    DcRenderContext context{};
    context.factory = factory_result.Value();

    const D2D1_RENDER_TARGET_PROPERTIES render_target_properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    HRESULT hr =
        context.factory->CreateDCRenderTarget(&render_target_properties, &context.render_target);
    if (FAILED(hr)) {
        return common::Result<DcRenderContext>::Failure(
            FormatHresultMessage(L"创建 Direct2D DCRenderTarget 失败，HRESULT=", hr));
    }

    hr = context.render_target->BindDC(hdc, &render_bounds);
    if (FAILED(hr)) {
        return common::Result<DcRenderContext>::Failure(
            FormatHresultMessage(L"绑定 Direct2D DC 失败，HRESULT=", hr));
    }

    const D2D1_COLOR_F brush_color = D2D1::ColorF(static_cast<float>(GetRValue(color)) / 255.0f,
                                                  static_cast<float>(GetGValue(color)) / 255.0f,
                                                  static_cast<float>(GetBValue(color)) / 255.0f,
                                                  1.0f);
    hr = context.render_target->CreateSolidColorBrush(brush_color, &context.brush);
    if (FAILED(hr)) {
        return common::Result<DcRenderContext>::Failure(
            FormatHresultMessage(L"创建 Direct2D 画刷失败，HRESULT=", hr));
    }

    return common::Result<DcRenderContext>::Success(std::move(context));
}

common::Result<Microsoft::WRL::ComPtr<ID2D1StrokeStyle>> CreateStrokeStyle(
    ID2D1Factory* factory,
    D2D1_CAP_STYLE start_cap,
    D2D1_CAP_STYLE end_cap,
    D2D1_LINE_JOIN line_join) {
    D2D1_STROKE_STYLE_PROPERTIES stroke_properties = D2D1::StrokeStyleProperties();
    stroke_properties.startCap = start_cap;
    stroke_properties.endCap = end_cap;
    stroke_properties.lineJoin = line_join;

    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> stroke_style;
    const HRESULT hr =
        factory->CreateStrokeStyle(&stroke_properties, nullptr, 0, &stroke_style);
    if (FAILED(hr)) {
        return common::Result<Microsoft::WRL::ComPtr<ID2D1StrokeStyle>>::Failure(
            FormatHresultMessage(L"创建 Direct2D 描边样式失败，HRESULT=", hr));
    }

    return common::Result<Microsoft::WRL::ComPtr<ID2D1StrokeStyle>>::Success(
        std::move(stroke_style));
}

}  // namespace

namespace common {

Result<void> DrawRectangleOnHdc(HDC hdc,
                                const RECT& render_bounds,
                                const RECT* clip_rect,
                                const RECT& rect,
                                COLORREF color,
                                float thickness) {
    if (!IsValidRect(rect)) {
        return Result<void>::Failure(L"矩形区域无效。");
    }

    auto context_result = PrepareDcRenderTarget(hdc, render_bounds, color);
    if (!context_result) {
        return Result<void>::Failure(context_result.Error());
    }

    const DcRenderContext& context = context_result.Value();
    const float stroke_width = std::max(2.0f, thickness);

    context.render_target->BeginDraw();
    context.render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (HasClipRect(clip_rect)) {
        context.render_target->PushAxisAlignedClip(ToD2DRect(*clip_rect),
                                                   D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    context.render_target->DrawRectangle(ToD2DRect(rect), context.brush.Get(), stroke_width);
    if (HasClipRect(clip_rect)) {
        context.render_target->PopAxisAlignedClip();
    }

    const HRESULT hr = context.render_target->EndDraw();
    if (FAILED(hr)) {
        return Result<void>::Failure(
            FormatHresultMessage(L"Direct2D 绘制矩形失败，HRESULT=", hr));
    }

    return Result<void>::Success();
}

Result<void> DrawArrowOnHdc(HDC hdc,
                            const RECT& render_bounds,
                            const RECT* clip_rect,
                            const POINT& start,
                            const POINT& end,
                            COLORREF color,
                            float thickness) {
    const float dx = static_cast<float>(end.x - start.x);
    const float dy = static_cast<float>(end.y - start.y);
    const float length = std::hypot(dx, dy);
    if (length < 1.0f) {
        return Result<void>::Failure(L"箭头长度过短。");
    }

    auto context_result = PrepareDcRenderTarget(hdc, render_bounds, color);
    if (!context_result) {
        return Result<void>::Failure(context_result.Error());
    }

    const DcRenderContext& context = context_result.Value();
    auto stroke_style_result = CreateStrokeStyle(context.factory,
                                                 D2D1_CAP_STYLE_ROUND,
                                                 D2D1_CAP_STYLE_ROUND,
                                                 D2D1_LINE_JOIN_ROUND);
    if (!stroke_style_result) {
        return Result<void>::Failure(stroke_style_result.Error());
    }

    const float stroke_width = std::max(2.0f, thickness);
    const float unit_x = dx / length;
    const float unit_y = dy / length;
    const float head_length = std::max(14.0f, stroke_width * 4.0f);
    const float back_x = -unit_x * head_length;
    const float back_y = -unit_y * head_length;
    const float cos_angle = std::cos(kArrowHeadAngleRadians);
    const float sin_angle = std::sin(kArrowHeadAngleRadians);

    const D2D1_POINT_2F start_point = ToD2DPoint(start);
    const D2D1_POINT_2F end_point = ToD2DPoint(end);
    const D2D1_POINT_2F left_point =
        D2D1::Point2F(static_cast<float>(end.x) + (back_x * cos_angle) - (back_y * sin_angle),
                      static_cast<float>(end.y) + (back_x * sin_angle) + (back_y * cos_angle));
    const D2D1_POINT_2F right_point =
        D2D1::Point2F(static_cast<float>(end.x) + (back_x * cos_angle) + (back_y * sin_angle),
                      static_cast<float>(end.y) - (back_x * sin_angle) + (back_y * cos_angle));

    context.render_target->BeginDraw();
    context.render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (HasClipRect(clip_rect)) {
        context.render_target->PushAxisAlignedClip(ToD2DRect(*clip_rect),
                                                   D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    context.render_target->DrawLine(start_point,
                                    end_point,
                                    context.brush.Get(),
                                    stroke_width,
                                    stroke_style_result.Value().Get());
    context.render_target->DrawLine(end_point,
                                    left_point,
                                    context.brush.Get(),
                                    stroke_width,
                                    stroke_style_result.Value().Get());
    context.render_target->DrawLine(end_point,
                                    right_point,
                                    context.brush.Get(),
                                    stroke_width,
                                    stroke_style_result.Value().Get());
    if (HasClipRect(clip_rect)) {
        context.render_target->PopAxisAlignedClip();
    }

    const HRESULT hr = context.render_target->EndDraw();
    if (FAILED(hr)) {
        return Result<void>::Failure(
            FormatHresultMessage(L"Direct2D 绘制箭头失败，HRESULT=", hr));
    }

    return Result<void>::Success();
}

}  // namespace common
