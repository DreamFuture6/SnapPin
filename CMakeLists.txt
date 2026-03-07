cmake_minimum_required(VERSION 3.22)
project(SnapPin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)

set(SOURCES
    src/main.cpp
    src/app/AppController.cpp
    src/app/TrayService.cpp
    src/app/HotkeyService.cpp
    src/core/Settings.cpp
    src/core/Logger.cpp
    src/core/CrashHandler.cpp
    src/core/Image.cpp
    src/core/ImageCodecUtil.cpp
    src/core/AnnotationRenderer.cpp
    src/core/OcrService.cpp
    src/core/ScreenCaptureUtil.cpp
    src/core/CaptureService.cpp
    src/core/Exporter.cpp
    src/core/ClipboardService.cpp
    src/core/HistoryService.cpp
    src/ui/OverlayWindow.cpp
    src/ui/ToolbarWindow.cpp
    src/ui/PinWindow.cpp
    src/ui/HistoryWindow.cpp
    src/ui/OcrResultPopupWindow.cpp
    src/ui/UiUtil.cpp
    src/ui/GdiBitmapBuffer.cpp
    src/ui/GdiResourceCache.cpp
    src/ui/FontManager.cpp
    src/ui/WindowUtil.cpp
    src/ui/widgets/CheckBox.cpp
    src/ui/widgets/TextEdit.cpp
    src/ui/SettingsWindow.cpp
    src/SnapPin.rc
)

set(HEADERS
    src/app/AppController.h
    src/app/TrayService.h
    src/app/HotkeyService.h
    src/app/HotkeyWrapper.h
    src/core/Settings.h
    src/core/Logger.h
    src/core/CrashHandler.h
    src/core/Image.h
    src/core/ImageCodecUtil.h
    src/core/AnnotationRenderer.h
    src/core/OcrService.h
    src/core/ScreenCaptureUtil.h
    src/core/Annotation.h
    src/core/CaptureService.h
    src/core/Exporter.h
    src/core/ClipboardService.h
    src/core/HistoryService.h
    src/ui/OverlayWindow.h
    src/ui/ToolbarWindow.h
    src/ui/PinWindow.h
    src/ui/HistoryWindow.h
    src/ui/OcrResultPopupWindow.h
    src/ui/GdiObject.h
    src/ui/UiUtil.h
    src/ui/GdiBitmapBuffer.h
    src/ui/WindowUtil.h
    src/ui/WindowProc.h
    src/ui/widgets/CheckBox.h
    src/ui/widgets/TextEdit.h
    src/ui/SettingsWindow.h
    src/common.h
    src/common/ScopedHandle.h
    src/app/AppIds.h
    src/resource.h
)

add_executable(SnapPin WIN32 ${SOURCES} ${HEADERS})

target_include_directories(SnapPin PRIVATE src)

target_link_libraries(SnapPin PRIVATE
    user32
    gdi32
    dwmapi
    shell32
    shlwapi
    winhttp
    crypt32
    windowscodecs
    ole32
    comctl32
    gdiplus
)

if (MSVC)
    target_compile_options(SnapPin PRIVATE /W4 /permissive- /utf-8)

    # Release 下启用低风险体积优化：函数级链接、字符串合并、全程序优化
    # 使用生成表达式仅在 Release 配置生效
    target_compile_options(SnapPin PRIVATE
        $<$<CONFIG:Release>:/Gy>   # 函数级链接（function-level linking）
        $<$<CONFIG:Release>:/GF>   # 常量字符串合并
        $<$<CONFIG:Release>:/GL>   # 全程序优化（LTO）
        $<$<CONFIG:Release>:/O1>   # 优化为体积优先（低风险）
    )

    # 链接器优化，配合 /GL 使用
    target_link_options(SnapPin PRIVATE
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Release>:/INCREMENTAL:NO>  # 禁用增量链接，减小最终可执行文件
        $<$<CONFIG:Release>:/MAP:SnapPin.map> # 生成 map 文件用于体积分析（输出到 build 目录）
    )
endif()
