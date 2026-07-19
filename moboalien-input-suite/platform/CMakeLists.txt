#=============================================================================
# Platform Abstraction Library
#=============================================================================
# This library abstracts OS-specific APIs (sockets, threads, shared memory, etc.).
if(WIN32)
    add_library(platform_lib STATIC
        include/platform.h
        windows/platform_win.cpp
    )
    # Link against Windows Sockets and GDI+ for networking and image encoding.
    target_link_libraries(platform_lib PRIVATE ws2_32) 
else()
    # For non-Windows platforms, define an INTERFACE library.
    # The actual sources will be added in the platform-specific block below.
    add_library(platform_lib STATIC INTERFACE)
endif()
