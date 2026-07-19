cmake_minimum_required(VERSION 3.22)
project(moboalien LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------
include_directories(
    platform/include
    input/include
    controller/include
    controller/include/auth
    utils/include
    protocol/include
    crypto/include
    auth
    image_utils/include
)

if(DEFINED ANDROID)
    include_directories(screenshare/android)
endif()
include_directories(screenshare/include)

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(MOBOALIEN_SOURCES
    # Platform (Android POSIX)
    platform/android/platform_android.cpp

    # Input injector (Lemuroid/JNI)
    input/lemuroid/input_injector_lemuroid.cpp

    # Controller core
    controller/controller.cpp
    controller/handshake.cpp
    controller/packet_handlers.cpp

    # Protocol
    protocol/packet_codec.cpp
    protocol/packet_encryptor.cpp

    # Utils
    utils/logger.cpp
    utils/utils.cpp
    utils/signal_handler.cpp
    utils/sha256.cpp
    utils/config_file.cpp

    # Auth (no-op stub -- encryption out of scope)
    auth/auth_service.cpp
)

if(DEFINED ANDROID)
    list(APPEND MOBOALIEN_SOURCES
        image_utils/android/image_encoder_android.cpp
        screenshare/android/screen_capture_android.cpp
        screenshare/base_streamer.cpp
        screenshare/bitmap_differ.cpp
        screenshare/http_streamer.cpp
        screenshare/tcp_streamer.cpp
        screenshare/differential_streamer.cpp
    )
endif()

# JNI bridge (only when building for Android via Gradle)
if(DEFINED ANDROID)
    list(APPEND MOBOALIEN_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../lemuroid-moboalien/src/main/cpp/moboalien_jni.cpp
    )
endif()

add_library(moboalien SHARED ${MOBOALIEN_SOURCES})

set_target_properties(moboalien PROPERTIES LINKER_LANGUAGE CXX)

if(DEFINED ANDROID)
    target_compile_definitions(moboalien PRIVATE ANDROID)
endif()

target_link_libraries(moboalien
    log
    android
    atomic
    c++
)
