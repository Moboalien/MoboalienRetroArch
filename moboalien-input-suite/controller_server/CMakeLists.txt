#=============================================================================
# Controller Server Executables
#=============================================================================
add_executable(controller_server controller_server.cpp)
target_link_libraries(controller_server PRIVATE controller_lib auth_lib screen_share_lib)

add_executable(screen_client screen_client.cpp)
target_link_libraries(screen_client PRIVATE Ws2_32 controller_protocol_lib)
