#pragma once

#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <thread>

#include "common/Result.h"

namespace capture {

// The UI owns this object; capture, COM and encoding stay on the worker thread.
class ScreenRecordingService {
public:
    ~ScreenRecordingService();
    ScreenRecordingService() = default;
    ScreenRecordingService(const ScreenRecordingService&) = delete;
    ScreenRecordingService& operator=(const ScreenRecordingService&) = delete;

    common::Result<void> Start(const RECT& region, const std::wstring& path);
    common::Result<void> StartWindow(HWND window, const std::wstring& path);
    RECT CurrentRegion() const;
    void RequestStop();
    common::Result<std::wstring> Stop();
    bool IsActive() const;
    bool IsFinished() const;

private:
    common::Result<void> StartSource(const RECT& region, HWND window, const std::wstring& path);
    void Record(RECT region, HWND window, std::wstring path,
                std::promise<common::Result<void>> ready);

    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> finished_{false};
    std::mutex wait_mutex_;
    std::condition_variable wake_;
    std::wstring path_;
    std::wstring error_;
    mutable std::mutex region_mutex_;
    RECT current_region_{};
};

}  // namespace capture
