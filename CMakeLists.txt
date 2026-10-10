cmake_minimum_required(VERSION 3.24)

project(NativeScreenshot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(NativeScreenshot WIN32
    src/main.cpp
    src/app/App.cpp
    src/ui/MainWindow.cpp
    src/ui/TitleBar.cpp
    src/ui/RecordingOverlay.cpp
    src/ui/OverlayPrimitives.cpp
    src/ui/RegionSelectionBuffers.cpp
    src/ui/RegionSelectionOverlay.Geometry.cpp
    src/ui/RegionSelectionOverlay.Input.cpp
    src/ui/RegionSelectionOverlay.cpp
    src/ui/WindowEditingCanvas.cpp
    src/ui/RegionSelectionRenderer.cpp
    src/ui/SelectionEditToolbar.cpp
    src/ui/SelectionSession.cpp
    src/ui/WindowSelectionOverlay.cpp
    src/ui/TrayIconController.cpp
    src/editing/ImageMarkupService.cpp
    src/hotkey/HotkeyDefinition.cpp
    src/hotkey/HotkeyManager.cpp
    src/capture/CapturedImage.cpp
    src/capture/ScreenCaptureService.cpp
    src/capture/ScreenRecordingService.cpp
    src/capture/WgcCaptureSource.cpp
    src/capture/WgcScreenshotService.cpp
    src/capture/GifRecordingWriter.cpp
    src/capture/WindowLocator.cpp
    src/capture/WindowCaptureService.cpp
    src/capture/AutoSaveService.cpp
    src/capture/BitmapFileWriter.cpp
    src/capture/ImageClipboardWriter.cpp
    src/capture/FileClipboardWriter.cpp
    src/capture/ImageFileWriter.cpp
    src/settings/SettingsRepository.cpp
    src/common/Direct2D.cpp
    src/common/DpiAwareness.cpp
    src/common/RectUtils.cpp
    src/common/Win32Error.cpp
    include/app/App.h
    include/common/GdiResources.h
    include/ui/MainWindow.h
    include/ui/TitleBar.h
    include/ui/UiTypography.h
    include/ui/RecordingOverlay.h
    include/ui/OverlayPrimitives.h
    include/ui/RegionSelectionBuffers.h
    include/ui/RegionSelectionOverlay.h
    include/ui/WindowEditingCanvas.h
    include/ui/RegionSelectionRenderer.h
    include/ui/SelectionEditToolbar.h
    include/ui/SelectionSession.h
    include/ui/WindowSelectionOverlay.h
    include/ui/TrayIconController.h
    include/editing/ImageMarkupService.h
    include/editing/MarkupCommand.h
    include/hotkey/HotkeyDefinition.h
    include/hotkey/HotkeyManager.h
    include/capture/CapturedImage.h
    include/capture/DesktopSnapshot.h
    include/capture/ScreenCaptureService.h
    include/capture/ScreenRecordingService.h
    include/capture/WgcCaptureSource.h
    include/capture/WgcScreenshotService.h
    include/capture/GifRecordingWriter.h
    include/capture/RecordingFileFormat.h
    include/capture/WindowInfo.h
    include/capture/WindowLocator.h
    include/capture/WindowCaptureService.h
    include/capture/AutoSaveService.h
    include/capture/BitmapFileWriter.h
    include/capture/ImageClipboardWriter.h
    include/capture/FileClipboardWriter.h
    include/capture/ImageFileFormat.h
    include/capture/ImageFileWriter.h
    include/settings/AppSettings.h
    include/settings/SettingsRepository.h
    include/common/Direct2D.h
    include/common/DpiAwareness.h
    include/common/RectUtils.h
    include/common/Result.h
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
    D3d11
    Dxgi
    Windowsapp
    Dwmapi
    Gdi32
    Msimg32
    Mfplat
    Mfreadwrite
    Mfuuid
    Ole32
    Shell32
    User32
    Windowscodecs
)

include(CTest)
if(BUILD_TESTING)
    add_executable(FileClipboardIntegration
        tests/FileClipboardIntegration.cpp
        src/capture/FileClipboardWriter.cpp
        src/common/Win32Error.cpp
    )
    target_include_directories(FileClipboardIntegration PRIVATE include)
    target_compile_definitions(FileClipboardIntegration PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(FileClipboardIntegration PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    target_link_libraries(FileClipboardIntegration PRIVATE User32 Shell32)
    add_test(NAME FileClipboardIntegration COMMAND FileClipboardIntegration)
    set_tests_properties(FileClipboardIntegration PROPERTIES TIMEOUT 15)
    add_executable(GifRecordingIntegration tests/GifRecordingIntegration.cpp src/capture/GifRecordingWriter.cpp)
    target_include_directories(GifRecordingIntegration PRIVATE include)
    target_compile_definitions(GifRecordingIntegration PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(GifRecordingIntegration PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    target_link_libraries(GifRecordingIntegration PRIVATE Windowscodecs Ole32)
    add_test(NAME GifRecordingIntegration COMMAND GifRecordingIntegration)
    set_tests_properties(GifRecordingIntegration PROPERTIES TIMEOUT 30)
    add_executable(UiTypographyIntegration
        tests/UiTypographyIntegration.cpp
        src/ui/OverlayPrimitives.cpp
        src/ui/WindowEditingCanvas.cpp
        src/ui/RegionSelectionBuffers.cpp
        src/ui/RegionSelectionOverlay.cpp
        src/ui/RegionSelectionOverlay.Geometry.cpp
        src/ui/RegionSelectionOverlay.Input.cpp
        src/ui/RegionSelectionRenderer.cpp
        src/ui/SelectionSession.cpp
        src/ui/SelectionEditToolbar.cpp
        src/editing/ImageMarkupService.cpp
        src/common/Direct2D.cpp
        src/capture/CapturedImage.cpp
        src/common/RectUtils.cpp
        src/ui/RecordingOverlay.cpp
        src/ui/TitleBar.cpp
        src/ui/TrayIconController.cpp
        src/common/DpiAwareness.cpp
        src/common/Win32Error.cpp
    )
    target_include_directories(UiTypographyIntegration PRIVATE include)
    target_compile_definitions(UiTypographyIntegration PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(UiTypographyIntegration PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    target_link_libraries(UiTypographyIntegration PRIVATE Comctl32 Shell32 Gdi32 User32 Msimg32 D2d1 Ole32)
    add_test(NAME UiTypographyIntegration COMMAND UiTypographyIntegration)
    set_tests_properties(UiTypographyIntegration PROPERTIES TIMEOUT 15)

    add_executable(ScreenRecordingIntegration
        tests/ScreenRecordingIntegration.cpp
        src/ui/RecordingOverlay.cpp
        src/capture/ScreenRecordingService.cpp
        src/capture/WgcCaptureSource.cpp
        src/capture/GifRecordingWriter.cpp
        src/capture/WindowCaptureService.cpp
        src/capture/WgcScreenshotService.cpp
        src/capture/WindowLocator.cpp
        src/capture/CapturedImage.cpp
        src/common/RectUtils.cpp
        src/common/Win32Error.cpp
        src/common/DpiAwareness.cpp
        src/settings/SettingsRepository.cpp
        src/hotkey/HotkeyDefinition.cpp
    )
    target_include_directories(ScreenRecordingIntegration PRIVATE include)
    target_compile_definitions(ScreenRecordingIntegration PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(ScreenRecordingIntegration PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    target_link_libraries(ScreenRecordingIntegration PRIVATE Mfplat Mfreadwrite Mfuuid Ole32 Gdi32 User32 Dwmapi Windowscodecs D3d11 Dxgi Windowsapp)
    add_test(NAME ScreenRecordingIntegration COMMAND ScreenRecordingIntegration)
    set_tests_properties(ScreenRecordingIntegration PROPERTIES TIMEOUT 60)

    add_executable(ScreenshotIntegration
        tests/ScreenshotIntegration.cpp
        src/capture/CapturedImage.cpp
        src/capture/ScreenCaptureService.cpp
        src/capture/WindowCaptureService.cpp
        src/capture/WindowLocator.cpp
        src/capture/WgcScreenshotService.cpp
        src/capture/WgcCaptureSource.cpp
        src/capture/ImageFileWriter.cpp
        src/capture/BitmapFileWriter.cpp
        src/common/DpiAwareness.cpp
        src/common/RectUtils.cpp
        src/common/Win32Error.cpp
    )
    target_include_directories(ScreenshotIntegration PRIVATE include)
    target_compile_definitions(ScreenshotIntegration PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(ScreenshotIntegration PRIVATE /W4 /EHsc /permissive- /utf-8)
    endif()
    target_link_libraries(ScreenshotIntegration PRIVATE D3d11 Dxgi Windowsapp Dwmapi Gdi32 User32 Ole32 Windowscodecs)
    add_test(NAME ScreenshotIntegration COMMAND ScreenshotIntegration)
    set_tests_properties(ScreenshotIntegration PROPERTIES TIMEOUT 30)
endif()
