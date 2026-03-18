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
    src/core/MediaFoundationUtil.cpp
    src/core/FfmpegExporter.cpp
    src/core/ScreenRecorder.cpp
    src/ui/OverlayWindow.cpp
    src/ui/ToolbarWindow.cpp
    src/ui/PinWindow.cpp
    src/ui/HistoryWindow.cpp
    src/ui/OcrResultPopupWindow.cpp
    src/ui/PreviewBarWindow.cpp
    src/ui/VideoPreviewPlayer.cpp
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
    src/core/MediaFoundationUtil.h
    src/core/FfmpegExporter.h
    src/core/ScreenRecorder.h
    src/ui/OverlayWindow.h
    src/ui/ToolbarWindow.h
    src/ui/PinWindow.h
    src/ui/HistoryWindow.h
    src/ui/OcrResultPopupWindow.h
    src/ui/PreviewBarWindow.h
    src/ui/VideoPreviewPlayer.h
    src/ui/GdiObject.h
    src/ui/UiUtil.h
    src/ui/GdiBitmapBuffer.h
    src/ui/WindowUtil.h
    src/ui/WindowProc.h
    src/ui/widgets/CheckBox.h
    src/ui/widgets/TextEdit.h
    src/ui/SettingsWindow.h
    src/common.h
    src/common/WindowMessagePayload.h
    src/common/ScopedHandle.h
    src/common/KnownFolderUtil.h
    src/app/AppIds.h
    src/resource.h
)

add_executable(SnapPin WIN32 ${SOURCES} ${HEADERS})

target_include_directories(SnapPin PRIVATE src)

set(SNAPPIN_FFMPEG_DOWNLOAD_URL
    "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/ffmpeg-master-latest-win64-gpl-shared.zip"
    CACHE STRING "FFmpeg SDK download URL")
set(SNAPPIN_FFMPEG_VENDOR_DIR "${CMAKE_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH "Local FFmpeg SDK directory in source tree")
set(SNAPPIN_FFMPEG_CACHE_DIR "${CMAKE_BINARY_DIR}/_deps/ffmpeg" CACHE PATH "FFmpeg download cache directory")
set(SNAPPIN_FFMPEG_ARCHIVE "${SNAPPIN_FFMPEG_CACHE_DIR}/ffmpeg-win64-shared.zip")
set(SNAPPIN_FFMPEG_EXTRACT_DIR "${SNAPPIN_FFMPEG_CACHE_DIR}/extracted")
set(SNAPPIN_FFMPEG_MIN_ARCHIVE_SIZE 70000000 CACHE STRING "FFmpeg archive minimal valid size in bytes")
set(SNAPPIN_FFMPEG_DOWNLOAD_TIMEOUT 7200 CACHE STRING "FFmpeg download timeout (seconds)")
set(SNAPPIN_FFMPEG_DOWNLOAD_INACTIVITY_TIMEOUT 180 CACHE STRING "FFmpeg download inactivity timeout (seconds)")

function(snappin_locate_ffmpeg_root BASE_DIR OUT_VAR)
    set(_snappin_found_root "")
    if (EXISTS "${BASE_DIR}/include/libavformat/avformat.h")
        set(_snappin_found_root "${BASE_DIR}")
    else()
        file(GLOB _snappin_root_candidates "${BASE_DIR}/*")
        foreach(candidate IN LISTS _snappin_root_candidates)
            if (EXISTS "${candidate}/include/libavformat/avformat.h")
                set(_snappin_found_root "${candidate}")
                break()
            endif()
        endforeach()
    endif()
    set(${OUT_VAR} "${_snappin_found_root}" PARENT_SCOPE)
endfunction()

set(SNAPPIN_FFMPEG_ROOT "")
snappin_locate_ffmpeg_root("${SNAPPIN_FFMPEG_VENDOR_DIR}" SNAPPIN_FFMPEG_ROOT)
if (SNAPPIN_FFMPEG_ROOT)
    message(STATUS "Using local FFmpeg SDK from ${SNAPPIN_FFMPEG_ROOT}")
else()
    file(MAKE_DIRECTORY "${SNAPPIN_FFMPEG_CACHE_DIR}")
    if (EXISTS "${SNAPPIN_FFMPEG_ARCHIVE}")
        file(SIZE "${SNAPPIN_FFMPEG_ARCHIVE}" SNAPPIN_FFMPEG_ARCHIVE_SIZE)
        if (SNAPPIN_FFMPEG_ARCHIVE_SIZE LESS SNAPPIN_FFMPEG_MIN_ARCHIVE_SIZE)
            message(WARNING "Found incomplete FFmpeg archive, removing: ${SNAPPIN_FFMPEG_ARCHIVE}")
            file(REMOVE "${SNAPPIN_FFMPEG_ARCHIVE}")
        endif()
    endif()

    if (NOT EXISTS "${SNAPPIN_FFMPEG_ARCHIVE}")
        message(STATUS "Downloading FFmpeg SDK from ${SNAPPIN_FFMPEG_DOWNLOAD_URL}")
        file(DOWNLOAD
            "${SNAPPIN_FFMPEG_DOWNLOAD_URL}"
            "${SNAPPIN_FFMPEG_ARCHIVE}"
            SHOW_PROGRESS
            STATUS SNAPPIN_FFMPEG_DOWNLOAD_STATUS
            TLS_VERIFY ON
            TIMEOUT "${SNAPPIN_FFMPEG_DOWNLOAD_TIMEOUT}"
            INACTIVITY_TIMEOUT "${SNAPPIN_FFMPEG_DOWNLOAD_INACTIVITY_TIMEOUT}")
        list(GET SNAPPIN_FFMPEG_DOWNLOAD_STATUS 0 SNAPPIN_FFMPEG_DOWNLOAD_CODE)
        list(GET SNAPPIN_FFMPEG_DOWNLOAD_STATUS 1 SNAPPIN_FFMPEG_DOWNLOAD_MESSAGE)
        if (NOT SNAPPIN_FFMPEG_DOWNLOAD_CODE EQUAL 0)
            message(FATAL_ERROR "FFmpeg download failed: ${SNAPPIN_FFMPEG_DOWNLOAD_MESSAGE}")
        endif()
    endif()

    file(SIZE "${SNAPPIN_FFMPEG_ARCHIVE}" SNAPPIN_FFMPEG_ARCHIVE_SIZE)
    if (SNAPPIN_FFMPEG_ARCHIVE_SIZE LESS SNAPPIN_FFMPEG_MIN_ARCHIVE_SIZE)
        message(FATAL_ERROR
            "FFmpeg archive is too small after download (${SNAPPIN_FFMPEG_ARCHIVE_SIZE} bytes), "
            "expected at least ${SNAPPIN_FFMPEG_MIN_ARCHIVE_SIZE} bytes. "
            "Please check network stability and run CMake configure again.")
    endif()

    set(SNAPPIN_FFMPEG_EXTRACT_ROOT "")
    snappin_locate_ffmpeg_root("${SNAPPIN_FFMPEG_EXTRACT_DIR}" SNAPPIN_FFMPEG_EXTRACT_ROOT)
    if (NOT SNAPPIN_FFMPEG_EXTRACT_ROOT)
        file(REMOVE_RECURSE "${SNAPPIN_FFMPEG_EXTRACT_DIR}")
        file(MAKE_DIRECTORY "${SNAPPIN_FFMPEG_EXTRACT_DIR}")
        message(STATUS "Extracting FFmpeg SDK...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xf "${SNAPPIN_FFMPEG_ARCHIVE}"
            WORKING_DIRECTORY "${SNAPPIN_FFMPEG_EXTRACT_DIR}"
            RESULT_VARIABLE SNAPPIN_FFMPEG_EXTRACT_RESULT)
        if (NOT SNAPPIN_FFMPEG_EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "FFmpeg extract failed with code ${SNAPPIN_FFMPEG_EXTRACT_RESULT}")
        endif()
        snappin_locate_ffmpeg_root("${SNAPPIN_FFMPEG_EXTRACT_DIR}" SNAPPIN_FFMPEG_EXTRACT_ROOT)
    endif()

    if (NOT SNAPPIN_FFMPEG_EXTRACT_ROOT)
        message(FATAL_ERROR "Unable to locate FFmpeg SDK root after extraction: ${SNAPPIN_FFMPEG_EXTRACT_DIR}")
    endif()

    message(STATUS "Persisting FFmpeg SDK into source tree: ${SNAPPIN_FFMPEG_VENDOR_DIR}")
    file(REMOVE_RECURSE "${SNAPPIN_FFMPEG_VENDOR_DIR}")
    file(MAKE_DIRECTORY "${SNAPPIN_FFMPEG_VENDOR_DIR}")
    file(COPY "${SNAPPIN_FFMPEG_EXTRACT_ROOT}/" DESTINATION "${SNAPPIN_FFMPEG_VENDOR_DIR}")
    snappin_locate_ffmpeg_root("${SNAPPIN_FFMPEG_VENDOR_DIR}" SNAPPIN_FFMPEG_ROOT)
    if (NOT SNAPPIN_FFMPEG_ROOT)
        message(FATAL_ERROR "Unable to persist FFmpeg SDK to source tree: ${SNAPPIN_FFMPEG_VENDOR_DIR}")
    endif()
endif()

set(SNAPPIN_FFMPEG_INCLUDE_DIR "${SNAPPIN_FFMPEG_ROOT}/include")
set(SNAPPIN_FFMPEG_LIB_DIR "${SNAPPIN_FFMPEG_ROOT}/lib")
set(SNAPPIN_FFMPEG_BIN_DIR "${SNAPPIN_FFMPEG_ROOT}/bin")

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
    mfplat
    mfreadwrite
    mfuuid
    mfplay
    evr
    propsys
    mmdevapi
    avrt
)

target_compile_definitions(SnapPin PRIVATE SNAPPIN_HAS_FFMPEG=1)
target_include_directories(SnapPin PRIVATE "${SNAPPIN_FFMPEG_INCLUDE_DIR}")

set(SNAPPIN_FFMPEG_REQUIRED_LIBS avcodec avformat avutil swscale)
foreach(fflib IN LISTS SNAPPIN_FFMPEG_REQUIRED_LIBS)
    unset(SNAPPIN_${fflib}_LIB CACHE)
    find_library(SNAPPIN_${fflib}_LIB
        NAMES ${fflib}
        PATHS "${SNAPPIN_FFMPEG_LIB_DIR}"
        NO_DEFAULT_PATH)
    if (NOT SNAPPIN_${fflib}_LIB)
        message(FATAL_ERROR "FFmpeg import library not found: ${fflib}.lib in ${SNAPPIN_FFMPEG_LIB_DIR}")
    endif()
    target_link_libraries(SnapPin PRIVATE "${SNAPPIN_${fflib}_LIB}")
endforeach()

set(SNAPPIN_FFMPEG_RUNTIME_DLLS "")
set(SNAPPIN_FFMPEG_REQUIRED_DLL_PREFIXES avcodec avformat avutil swscale)
foreach(ffdll IN LISTS SNAPPIN_FFMPEG_REQUIRED_DLL_PREFIXES)
    file(GLOB SNAPPIN_FFMPEG_${ffdll}_DLLS "${SNAPPIN_FFMPEG_BIN_DIR}/${ffdll}*.dll")
    if (NOT SNAPPIN_FFMPEG_${ffdll}_DLLS)
        message(FATAL_ERROR "FFmpeg runtime DLL not found: ${ffdll}*.dll in ${SNAPPIN_FFMPEG_BIN_DIR}")
    endif()
    list(APPEND SNAPPIN_FFMPEG_RUNTIME_DLLS ${SNAPPIN_FFMPEG_${ffdll}_DLLS})
endforeach()
list(REMOVE_DUPLICATES SNAPPIN_FFMPEG_RUNTIME_DLLS)
add_custom_command(TARGET SnapPin POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DFFMPEG_BIN_DIR=${SNAPPIN_FFMPEG_BIN_DIR}
            -DFFMPEG_TARGET_DIR=$<TARGET_FILE_DIR:SnapPin>
            -P ${CMAKE_SOURCE_DIR}/cmake/SyncFfmpegRuntime.cmake)

if (MSVC)
    target_compile_options(SnapPin PRIVATE /W4 /permissive- /utf-8)

    target_compile_options(SnapPin PRIVATE
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/GF>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/O1>
    )

    target_link_options(SnapPin PRIVATE
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
        $<$<CONFIG:Release>:/MAP:SnapPin.map>
    )
endif()
