#=============================================================================
# Image Utils Library
#=============================================================================
# Image processing and encoding utilities.
add_library(image_utils_lib STATIC
    include/i_image_encoder.h
    include/image_utils.h
    windows/image_encoder.cpp
)
if(WIN32)
    # Check for NASM to ensure SIMD acceleration is enabled.
    # Only use libjpeg-turbo if NASM is present for SIMD. Otherwise fall back to GDI+.
    find_program(NASM_PATH NAMES nasm nasm.exe)
    message(STATUS "NASM detection result: ${NASM_PATH}")
    
    if(NASM_PATH)
        message(STATUS "NASM found. Enabling libjpeg-turbo with SIMD acceleration.")
        include(FetchContent)

        # Create a patch script to bypass libjpeg-turbo's check preventing add_subdirectory
        file(WRITE ${CMAKE_BINARY_DIR}/libjpeg_patch.cmake "
        file(READ CMakeLists.txt content)

        # 0. Fix cmake_minimum_required deprecation warning
        string(REPLACE \"cmake_minimum_required(VERSION 2.8.12)\" \"cmake_minimum_required(VERSION 3.15)\" content \"\${content}\")

        # 1. Bypass the 'In-tree build' restriction
        string(REPLACE \"message(FATAL_ERROR \\\"The libjpeg-turbo\" \"message(WARNING \\\"The libjpeg-turbo\" content \"\${content}\")

        # 2. Targeted Strike: Only remove commands that mention the executable names
        # This regex looks for commands containing cjpeg, djpeg, jpegtran, tjbench, rdjpgcom, wrjpgcom, or md5cmp
        string(REGEX REPLACE \"(add_executable|target_link_libraries|install|set_property|set_target_properties)\\\\s*\\\\([^)]*(cjpeg|djpeg|jpegtran|tjbench|rdjpgcom|wrjpgcom|md5cmp|tjunittest|example|strtest)[^)]*\\\\)\" \"# [[Removed Executable Command]]\" content \"\${content}\")

        file(WRITE CMakeLists.txt \"\${content}\")
        ")
        FetchContent_Declare(
            libjpeg-turbo
            URL https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.0.1.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/libjpeg_patch.cmake
        )

        set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
        set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
        set(WITH_TURBOJPEG ON CACHE BOOL "" FORCE)
        set(ENABLE_CJPEG OFF CACHE BOOL "" FORCE)
        set(ENABLE_DJPEG OFF CACHE BOOL "" FORCE)
        set(ENABLE_JPEGTRAN OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(WITH_TURBOJPEG_OFF OFF CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(libjpeg-turbo)

        target_link_libraries(image_utils_lib PRIVATE gdiplus jpeg-static turbojpeg-static)
        target_include_directories(image_utils_lib PRIVATE ${libjpeg-turbo_SOURCE_DIR} ${libjpeg-turbo_BINARY_DIR})
        target_compile_definitions(image_utils_lib PRIVATE USE_LIBJPEG_TURBO LIBJPEG_TURBO_WITH_SIMD)
    else()
        message(WARNING "NASM not found! libjpeg-turbo will be DISABLED. Falling back to GDI+ for JPEG encoding.")
        target_link_libraries(image_utils_lib PRIVATE gdiplus)
    endif()
endif()
