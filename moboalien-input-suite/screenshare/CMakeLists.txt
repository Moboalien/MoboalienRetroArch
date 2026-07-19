#=============================================================================
# Screen Sharing Library and Executables
#=============================================================================

# --- Screen Sharing Core Library ---
# This library contains the logic for screen capture, encoding, and streaming.
add_library(screen_share_lib STATIC
    base_streamer.cpp
    tcp_streamer.cpp
    udp_streamer.cpp
    bitmap_differ.cpp
    http_streamer.cpp
    differential_streamer.cpp
    PipelineFactory.cpp
)

if(FFMPEG_FOUND)
    target_sources(screen_share_lib PRIVATE
        avcodec_streamer.cpp
        avcodec_pipeline.cpp
    )
endif()

if(FFMPEG_FOUND)
    target_include_directories(screen_share_lib PUBLIC ${FFMPEG_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(screen_share_lib PUBLIC ${FFMPEG_AVCODEC_LIBRARY} ${FFMPEG_AVUTIL_LIBRARY} ${FFMPEG_LIBRARIES})

    if(WIN32)
        # Fetch MFX headers (mfx_dispatch has correct mfx/ subdirectory structure)
        include(FetchContent)
        FetchContent_Declare(
            mfx_headers
            GIT_REPOSITORY https://github.com/lu-zero/mfx_dispatch.git
            GIT_TAG master
        )
        FetchContent_GetProperties(mfx_headers)
        if(NOT mfx_headers_POPULATED)
            FetchContent_Populate(mfx_headers)
        endif()
        # Include the root directory so mfx/mfxvideo.h resolves correctly
        target_include_directories(screen_share_lib PRIVATE ${mfx_headers_SOURCE_DIR})
    endif()
endif()

# Add platform-specific sources and link platform-specific libraries.
if(WIN32)
    target_sources(screen_share_lib PRIVATE
        windows/windows_screen_capture.cpp
    )
    if(FFMPEG_FOUND)
        target_sources(screen_share_lib PRIVATE
            windows/avcodec_cpu_pipeline.cpp
        )
    endif()
    target_link_libraries(screen_share_lib PUBLIC platform_lib image_utils_lib utils_lib Gdi32 mf mfplat mfreadwrite mfuuid ole32)
else()
    target_sources(screen_share_lib PRIVATE
        linux/linux_screen_capture.cpp
    )
    target_link_libraries(screen_share_lib PRIVATE platform_lib X11 Xext Xfixes)
endif()

# --- Screen Server Executable ---
add_executable(screen_server screen_server.cpp)
target_link_libraries(screen_server PRIVATE screen_share_lib d3d11 d3dcompiler)

# Copy FFmpeg DLLs next to executables (screen_server is now defined)
if(FFMPEG_DLLS_TO_COPY)
    foreach(_dll ${FFMPEG_DLLS_TO_COPY})
        message(STATUS "[FFmpeg] Registering POST_BUILD copy: ${_dll}")
        add_custom_command(TARGET screen_server POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "[FFmpeg] Copying ${_dll}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:screen_server>"
        )
    endforeach()
endif()
