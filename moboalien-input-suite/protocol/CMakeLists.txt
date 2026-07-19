#=============================================================================
# Controller Protocol Library
#=============================================================================
# Handles encoding and decoding of controller-specific network packets.
add_library(controller_protocol_lib STATIC
    packet_codec.cpp
    packet_encryptor.cpp
)
target_link_libraries(controller_protocol_lib PRIVATE utils_lib crypto_lib platform_lib)
