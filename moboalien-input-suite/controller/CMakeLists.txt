#=============================================================================
# Controller Library
#=============================================================================
# This library contains the core controller logic and packet handlers.
add_library(controller_lib STATIC
    controller.cpp
    packet_handlers.cpp
    handshake.cpp
)
target_include_directories(controller_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../screenshare/include)

target_link_libraries(controller_lib PUBLIC platform_lib input_lib controller_protocol_lib crypto_lib)
