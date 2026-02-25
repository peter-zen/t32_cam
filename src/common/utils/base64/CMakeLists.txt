# add library
add_library(common_utils_base64 SHARED Base64.cpp)

# include header file
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# link libraries
target_link_libraries(common_utils_base64 PRIVATE logger)

# Set the output directory for the library
set_target_properties(common_utils_base64 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
