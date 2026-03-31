#pragma once

#include <Windows.h>

#include <utility>

namespace common {

template <typename HandleType>
class UniqueGdiObject {
public:
    UniqueGdiObject() = default;
    explicit UniqueGdiObject(HandleType handle) : handle_(handle) {}

    ~UniqueGdiObject() {
        Reset();
    }

    UniqueGdiObject(const UniqueGdiObject&) = delete;
    UniqueGdiObject& operator=(const UniqueGdiObject&) = delete;

    UniqueGdiObject(UniqueGdiObject&& other) noexcept : handle_(other.Release()) {}

    UniqueGdiObject& operator=(UniqueGdiObject&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    [[nodiscard]] HandleType Get() const {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const {
        return handle_ != nullptr;
    }

    [[nodiscard]] HandleType Release() {
        HandleType released = handle_;
        handle_ = nullptr;
        return released;
    }

    void Reset(HandleType handle = nullptr) {
        if (handle_ != nullptr) {
            DeleteObject(handle_);
        }
        handle_ = handle;
    }

private:
    HandleType handle_ = nullptr;
};

using UniqueBitmap = UniqueGdiObject<HBITMAP>;
using UniqueBrush = UniqueGdiObject<HBRUSH>;
using UniquePen = UniqueGdiObject<HPEN>;
using UniqueRegion = UniqueGdiObject<HRGN>;

class UniqueDc {
public:
    UniqueDc() = default;
    explicit UniqueDc(HDC handle) : handle_(handle) {}

    ~UniqueDc() {
        Reset();
    }

    UniqueDc(const UniqueDc&) = delete;
    UniqueDc& operator=(const UniqueDc&) = delete;

    UniqueDc(UniqueDc&& other) noexcept : handle_(other.Release()) {}

    UniqueDc& operator=(UniqueDc&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    [[nodiscard]] HDC Get() const {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const {
        return handle_ != nullptr;
    }

    [[nodiscard]] HDC Release() {
        HDC released = handle_;
        handle_ = nullptr;
        return released;
    }

    void Reset(HDC handle = nullptr) {
        if (handle_ != nullptr) {
            DeleteDC(handle_);
        }
        handle_ = handle;
    }

private:
    HDC handle_ = nullptr;
};

class WindowDcHandle {
public:
    WindowDcHandle() = default;
    WindowDcHandle(HWND window, HDC handle) : window_(window), handle_(handle) {}

    ~WindowDcHandle() {
        Reset();
    }

    WindowDcHandle(const WindowDcHandle&) = delete;
    WindowDcHandle& operator=(const WindowDcHandle&) = delete;

    WindowDcHandle(WindowDcHandle&& other) noexcept
        : window_(other.window_), handle_(other.Release()) {
        other.window_ = nullptr;
    }

    WindowDcHandle& operator=(WindowDcHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            window_ = other.window_;
            handle_ = other.Release();
            other.window_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HDC Get() const {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const {
        return handle_ != nullptr;
    }

    [[nodiscard]] HDC Release() {
        HDC released = handle_;
        handle_ = nullptr;
        return released;
    }

    void Reset(HWND window = nullptr, HDC handle = nullptr) {
        if (handle_ != nullptr) {
            ReleaseDC(window_, handle_);
        }
        window_ = window;
        handle_ = handle;
    }

private:
    HWND window_ = nullptr;
    HDC handle_ = nullptr;
};

class ScopedSelectObject {
public:
    ScopedSelectObject() = default;
    ScopedSelectObject(HDC dc, HGDIOBJ object) {
        Select(dc, object);
    }

    ~ScopedSelectObject() {
        Reset();
    }

    ScopedSelectObject(const ScopedSelectObject&) = delete;
    ScopedSelectObject& operator=(const ScopedSelectObject&) = delete;

    ScopedSelectObject(ScopedSelectObject&& other) noexcept
        : dc_(other.dc_), previous_(other.previous_) {
        other.dc_ = nullptr;
        other.previous_ = nullptr;
    }

    ScopedSelectObject& operator=(ScopedSelectObject&& other) noexcept {
        if (this != &other) {
            Reset();
            dc_ = other.dc_;
            previous_ = other.previous_;
            other.dc_ = nullptr;
            other.previous_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] bool IsValid() const {
        return dc_ != nullptr && previous_ != nullptr && previous_ != HGDI_ERROR;
    }

    [[nodiscard]] HGDIOBJ Previous() const {
        return previous_;
    }

    bool Select(HDC dc, HGDIOBJ object) {
        Reset();
        if (dc == nullptr || object == nullptr) {
            return false;
        }

        HGDIOBJ previous = SelectObject(dc, object);
        if (previous == nullptr || previous == HGDI_ERROR) {
            return false;
        }

        dc_ = dc;
        previous_ = previous;
        return true;
    }

    void Reset() {
        if (dc_ != nullptr && previous_ != nullptr && previous_ != HGDI_ERROR) {
            SelectObject(dc_, previous_);
        }

        dc_ = nullptr;
        previous_ = nullptr;
    }

private:
    HDC dc_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};

}  // namespace common
