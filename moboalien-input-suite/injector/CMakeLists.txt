#=============================================================================
# SendInput Helper Executable
#=============================================================================
add_executable(sendinput_helper sendinput_helper.cpp)
target_link_libraries(sendinput_helper PRIVATE ws2_32)
