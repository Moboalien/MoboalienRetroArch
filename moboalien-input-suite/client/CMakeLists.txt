#=============================================================================
# Client Libraries and Executables
#=============================================================================

# --- Handshake Client Library ---
# Client-side handshake functionality
add_library(handshake_client_lib STATIC
    handshake_client.cpp
)
target_link_libraries(handshake_client_lib PRIVATE controller_protocol_lib platform_lib crypto_lib)

# --- Input Client Executable ---
add_executable(input_client controller_client/input_client.cpp)
target_link_libraries(input_client PRIVATE ws2_32 handshake_client_lib controller_protocol_lib platform_lib crypto_lib)
