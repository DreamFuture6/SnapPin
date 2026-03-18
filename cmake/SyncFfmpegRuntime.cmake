if (NOT DEFINED FFMPEG_BIN_DIR)
    message(FATAL_ERROR "FFMPEG_BIN_DIR is not set")
endif()
if (NOT DEFINED FFMPEG_TARGET_DIR)
    message(FATAL_ERROR "FFMPEG_TARGET_DIR is not set")
endif()

set(_snappin_ffmpeg_prefixes avcodec avformat avutil swscale swresample)

foreach(prefix IN LISTS _snappin_ffmpeg_prefixes)
    file(GLOB old_dlls "${FFMPEG_TARGET_DIR}/${prefix}*.dll")
    if (old_dlls)
        file(REMOVE ${old_dlls})
    endif()
endforeach()

set(copied_count 0)
foreach(prefix IN LISTS _snappin_ffmpeg_prefixes)
    file(GLOB src_dlls "${FFMPEG_BIN_DIR}/${prefix}*.dll")
    foreach(src_dll IN LISTS src_dlls)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${src_dll}" "${FFMPEG_TARGET_DIR}"
            RESULT_VARIABLE copy_result)
        if (NOT copy_result EQUAL 0)
            message(FATAL_ERROR "Failed to copy FFmpeg runtime DLL: ${src_dll}")
        endif()
        math(EXPR copied_count "${copied_count} + 1")
    endforeach()
endforeach()

if (copied_count EQUAL 0)
    message(FATAL_ERROR "No FFmpeg runtime DLLs found under ${FFMPEG_BIN_DIR}")
endif()
