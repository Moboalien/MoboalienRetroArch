#=============================================================================
# Proxy DLLs (for hooking into games)
#=============================================================================
add_library(dinput8 SHARED dinput8/dinput8.cpp dinput8/dinput8.def)
target_link_libraries(dinput8 PRIVATE dxguid)
set_target_properties(dinput8 PROPERTIES OUTPUT_NAME "dinput8")

add_library(xinput SHARED xinput/xinput.cpp)
set_target_properties(xinput PROPERTIES OUTPUT_NAME "xinput1_4") # Name used by most games
