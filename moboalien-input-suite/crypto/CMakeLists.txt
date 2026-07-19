#=============================================================================
# Crypto Module Library
#=============================================================================
# This library handles RSA and AES encryption/decryption with platform-independent crypto.
add_library(crypto_lib STATIC
    include/i_crypto_helper.h
)
if(WIN32)
    target_sources(crypto_lib PRIVATE
        windows/crypto_helper_windows.cpp
        aes.c
    )
else()
    target_sources(crypto_lib PRIVATE
        windows/crypto_helper_linux.cpp
    )
    find_package(OpenSSL REQUIRED)
    target_link_libraries(crypto_lib PRIVATE OpenSSL::Crypto)
endif()
target_include_directories(crypto_lib PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Expose public header for dependency injection
target_include_directories(crypto_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
