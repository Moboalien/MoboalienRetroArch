#=============================================================================
# Auth Service Library
#=============================================================================
add_library(auth_lib STATIC
    auth_service.cpp
)
target_link_libraries(auth_lib PRIVATE crypto_lib)
target_include_directories(auth_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
