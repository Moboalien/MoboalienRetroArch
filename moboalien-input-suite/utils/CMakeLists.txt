#=============================================================================
# Utilities Library
#=============================================================================
# General-purpose helper functions (e.g., network byte order conversion).
add_library(utils_lib STATIC
    include/rect.h
    include/config_file.h
    include/sha256.h
    include/logger.h
    utils.cpp
    config_file.cpp
    sha256.cpp
    signal_handler.cpp
    logger.cpp
)
