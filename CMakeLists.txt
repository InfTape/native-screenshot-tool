cmake_minimum_required(VERSION 3.24)

project(NativeScreenshot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(NativeScreenshot WIN32
    src/main.cpp
    src/app/App.cpp
    src/ui/MainWindow.cpp
    src/ui/RegionSelectionOverlay.cpp
    src/ui/SelectionEditToolbar.cpp
    src/ui/WindowSelectionOverlay.cpp
    src/ui/TrayIconController.cpp
    src/editing/ImageMarkupService.cpp
    src/hotkey/HotkeyDefinition.cpp
    src/hotkey/HotkeyManager.cpp
    src/capture/CapturedImage.cpp
    src/capture/ScreenCaptureService.cpp
    src/capture/WindowLocator.cpp
    src/capture/WindowCaptureService.cpp
    src/capture/AutoSaveService.cpp
    src/capture/BitmapFileWriter.cpp
    src/capture/ImageClipboardWriter.cpp
    src/capture/ImageFileWriter.cpp
    src/settings/SettingsRepository.cpp
    src/common/Direct2D.cpp
    src/common/DpiAwareness.cpp
    src/common/RectUtils.cpp
    src/common/Win32Error.cpp
    include/app/App.h
    include/ui/MainWindow.h
    include/ui/RegionSelectionOverlay.h
    include/ui/SelectionEditToolbar.h
    include/ui/WindowSelectionOverlay.h
    include/ui/TrayIconController.h
    include/editing/ImageMarkupService.h
    include/hotkey/HotkeyDefinition.h
    include/hotkey/HotkeyManager.h
    include/capture/CapturedImage.h
    include/capture/DesktopSnapshot.h
    include/capture/ScreenCaptureService.h
    include/capture/WindowInfo.h
    include/capture/WindowLocator.h
    include/capture/WindowCaptureService.h
    include/capture/AutoSaveService.h
    include/capture/BitmapFileWriter.h
    include/capture/ImageClipboardWriter.h
    include/capture/ImageFileFormat.h
    include/capture/ImageFileWriter.h
    include/settings/AppSettings.h
    include/settings/SettingsRepository.h
    include/common/Direct2D.h
    include/common/DpiAwareness.h
    include/common/RectUtils.h
    include/common/Win32Error.h
)

target_include_directories(NativeScreenshot PRIVATE include)

target_compile_definitions(NativeScreenshot PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(NativeScreenshot PRIVATE /W4 /EHsc /permissive- /utf-8)
else()
    target_compile_options(NativeScreenshot PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(NativeScreenshot PRIVATE
    Comctl32
    Comdlg32
    D2d1
    Dwmapi
    Gdi32
    Msimg32
    Ole32
    Shell32
    User32
    Windowscodecs
)
