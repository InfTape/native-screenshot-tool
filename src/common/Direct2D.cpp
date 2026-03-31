#include "common/Direct2D.h"

#include <d2d1.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr float kArrowHeadAngleRadians = 0.5235988f;  // 30 degrees

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

}  // namespace

namespace common {

bool DrawArrowOnHdc(HDC hdc,
                    const RECT& render_bounds,
                    const RECT* clip_rect,
                    const POINT& start,
                    const POINT& end,
                    COLORREF color,
                    float thickness,
                    std::wstring& error_message) {
    error_message.clear();
    if (hdc == nullptr) {
        error_message = L"Direct2D 绘制目标无效。";
        return false;
    }

    if (render_bounds.left >= render_bounds.right || render_bounds.top >= render_bounds.bottom) {
        error_message = L"Direct2D 绘制范围无效。";
        return false;
    }

    const float dx = static_cast<float>(end.x - start.x);
    const float dy = static_cast<float>(end.y - start.y);
    const float length = std::hypot(dx, dy);
    if (length < 1.0f) {
        error_message = L"箭头长度过短。";
        return false;
    }

    ID2D1Factory* factory = GetDirect2DFactory(error_message);
    if (factory == nullptr) {
        return false;
    }

    const D2D1_RENDER_TARGET_PROPERTIES render_target_properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> render_target;
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

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    const D2D1_COLOR_F brush_color = D2D1::ColorF(static_cast<float>(GetRValue(color)) / 255.0f,
                                                  static_cast<float>(GetGValue(color)) / 255.0f,
                                                  static_cast<float>(GetBValue(color)) / 255.0f,
                                                  1.0f);
    hr = render_target->CreateSolidColorBrush(brush_color, &brush);
    if (FAILED(hr)) {
        error_message = FormatHresultMessage(L"创建 Direct2D 画刷失败，HRESULT=", hr);
        return false;
    }

    D2D1_STROKE_STYLE_PROPERTIES stroke_properties = D2D1::StrokeStyleProperties();
    stroke_properties.startCap = D2D1_CAP_STYLE_ROUND;
    stroke_properties.endCap = D2D1_CAP_STYLE_ROUND;
    stroke_properties.lineJoin = D2D1_LINE_JOIN_ROUND;

    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> stroke_style;
    hr = factory->CreateStrokeStyle(&stroke_properties, nullptr, 0, &stroke_style);
    if (FAILED(hr)) {
        error_message = FormatHresultMessage(L"创建 Direct2D 描边样式失败，HRESULT=", hr);
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
    if (clip_rect != nullptr && clip_rect->left < clip_rect->right && clip_rect->top < clip_rect->bottom) {
        render_target->PushAxisAlignedClip(
            D2D1::RectF(static_cast<float>(clip_rect->left),
                        static_cast<float>(clip_rect->top),
                        static_cast<float>(clip_rect->right),
                        static_cast<float>(clip_rect->bottom)),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    render_target->DrawLine(start_point, end_point, brush.Get(), stroke_width, stroke_style.Get());
    render_target->DrawLine(end_point, left_point, brush.Get(), stroke_width, stroke_style.Get());
    render_target->DrawLine(end_point, right_point, brush.Get(), stroke_width, stroke_style.Get());
    if (clip_rect != nullptr && clip_rect->left < clip_rect->right && clip_rect->top < clip_rect->bottom) {
        render_target->PopAxisAlignedClip();
    }
    hr = render_target->EndDraw();
    if (FAILED(hr)) {
        error_message = FormatHresultMessage(L"Direct2D 绘制箭头失败，HRESULT=", hr);
        return false;
    }

    return true;
}

}  // namespace common
