#include "common/Direct2D.h"

#include <d2d1.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr float kArrowHeadAngleRadians = 0.5235988f;  // 30 degrees

bool IsValidRect(const RECT& rect) {
    return rect.left < rect.right && rect.top < rect.bottom;
}

std::wstring FormatHresultMessage(const wchar_t* prefix, HRESULT hr) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return std::wstring(prefix) + buffer;
}

ID2D1Factory* GetDirect2DFactory(std::wstring& error_message) {
    static Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    if (factory) {
        return factory.Get();
    }

    const HRESULT hr =
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        error_message = FormatHresultMessage(L"创建 Direct2D 工厂失败，HRESULT=", hr);
        return nullptr;
    }

    return factory.Get();
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

bool PrepareDcRenderTarget(HDC hdc,
                           const RECT& render_bounds,
                           COLORREF color,
                           std::wstring& error_message,
                           ID2D1Factory*& factory,
                           Microsoft::WRL::ComPtr<ID2D1DCRenderTarget>& render_target,
                           Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>& brush) {
    if (hdc == nullptr) {
        error_message = L"Direct2D 绘制目标无效。";
        return false;
    }

    if (!IsValidRect(render_bounds)) {
        error_message = L"Direct2D 绘制范围无效。";
        return false;
    }

    factory = GetDirect2DFactory(error_message);
    if (factory == nullptr) {
        return false;
    }

    const D2D1_RENDER_TARGET_PROPERTIES render_target_properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    HRESULT hr = factory->CreateDCRenderTarget(&render_target_properties, &render_target);
    if (FAILED(hr)) {
        error_message = FormatHresultMessage(L"创建 Direct2D DCRenderTarget 失败，HRESULT=", hr);
        return false;
    }

    hr = render_target->BindDC(hdc, &render_bounds);
    if (FAILED(hr)) {
        error_message = FormatHresultMessage(L"绑定 Direct2D DC 失败，HRESULT=", hr);
        return false;
    }

    const D2D1_COLOR_F brush_color = D2D1::ColorF(static_cast<float>(GetRValue(color)) / 255.0f,
                                                  static_cast<float>(GetGValue(color)) / 255.0f,
                                                  static_cast<float>(GetBValue(color)) / 255.0f,
                                                  1.0f);
    hr = render_target->CreateSolidColorBrush(brush_color, &brush);
    if (FAILED(hr)) {
        error_message = FormatHresultMessage(L"创建 Direct2D 画刷失败，HRESULT=", hr);
        return false;
    }

    return true;
}

bool CreateStrokeStyle(ID2D1Factory* factory,
                       D2D1_CAP_STYLE start_cap,
                       D2D1_CAP_STYLE end_cap,
                       D2D1_LINE_JOIN line_join,
                       std::wstring& error_message,
                       Microsoft::WRL::ComPtr<ID2D1StrokeStyle>& stroke_style) {
    D2D1_STROKE_STYLE_PROPERTIES stroke_properties = D2D1::StrokeStyleProperties();
    stroke_properties.startCap = start_cap;
    stroke_properties.endCap = end_cap;
    stroke_properties.lineJoin = line_join;

    const HRESULT hr = factory->CreateStrokeStyle(&stroke_properties, nullptr, 0, &stroke_style);
    if (FAILED(hr)) {
        error_message = FormatHresultMessage(L"创建 Direct2D 描边样式失败，HRESULT=", hr);
        return false;
    }

    return true;
}

bool HasClipRect(const RECT* clip_rect) {
    return clip_rect != nullptr && IsValidRect(*clip_rect);
}

}  // namespace

namespace common {

bool DrawRectangleOnHdc(HDC hdc,
                        const RECT& render_bounds,
                        const RECT* clip_rect,
                        const RECT& rect,
                        COLORREF color,
                        float thickness,
                        std::wstring& error_message) {
    error_message.clear();
    if (!IsValidRect(rect)) {
        error_message = L"矩形区域无效。";
        return false;
    }

    ID2D1Factory* factory = nullptr;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> render_target;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (!PrepareDcRenderTarget(hdc, render_bounds, color, error_message, factory, render_target, brush)) {
        return false;
    }

    const float stroke_width = std::max(2.0f, thickness);

    render_target->BeginDraw();
    render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (HasClipRect(clip_rect)) {
        render_target->PushAxisAlignedClip(ToD2DRect(*clip_rect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    render_target->DrawRectangle(ToD2DRect(rect), brush.Get(), stroke_width);
    if (HasClipRect(clip_rect)) {
        render_target->PopAxisAlignedClip();
    }

    const HRESULT hr = render_target->EndDraw();
    if (FAILED(hr)) {
        error_message = FormatHresultMessage(L"Direct2D 绘制矩形失败，HRESULT=", hr);
        return false;
    }

    return true;
}

bool DrawArrowOnHdc(HDC hdc,
                    const RECT& render_bounds,
                    const RECT* clip_rect,
                    const POINT& start,
                    const POINT& end,
                    COLORREF color,
                    float thickness,
                    std::wstring& error_message) {
    error_message.clear();
    const float dx = static_cast<float>(end.x - start.x);
    const float dy = static_cast<float>(end.y - start.y);
    const float length = std::hypot(dx, dy);
    if (length < 1.0f) {
        error_message = L"箭头长度过短。";
        return false;
    }

    ID2D1Factory* factory = nullptr;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> render_target;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (!PrepareDcRenderTarget(hdc, render_bounds, color, error_message, factory, render_target, brush)) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> stroke_style;
    if (!CreateStrokeStyle(factory,
                           D2D1_CAP_STYLE_ROUND,
                           D2D1_CAP_STYLE_ROUND,
                           D2D1_LINE_JOIN_ROUND,
                           error_message,
                           stroke_style)) {
        return false;
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

    render_target->BeginDraw();
    render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (HasClipRect(clip_rect)) {
        render_target->PushAxisAlignedClip(ToD2DRect(*clip_rect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    render_target->DrawLine(start_point, end_point, brush.Get(), stroke_width, stroke_style.Get());
    render_target->DrawLine(end_point, left_point, brush.Get(), stroke_width, stroke_style.Get());
    render_target->DrawLine(end_point, right_point, brush.Get(), stroke_width, stroke_style.Get());
    if (HasClipRect(clip_rect)) {
        render_target->PopAxisAlignedClip();
    }
    const HRESULT hr = render_target->EndDraw();
    if (FAILED(hr)) {
        error_message = FormatHresultMessage(L"Direct2D 绘制箭头失败，HRESULT=", hr);
        return false;
    }

    return true;
}

}  // namespace common
