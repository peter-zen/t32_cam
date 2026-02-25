# add library
add_library(common_utils_jpeg SHARED Jpeg.cpp)

# include header file
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../logger
)

# link libraries
target_link_libraries(common_utils_jpeg PRIVATE logger)

# Set the output directory for the library
set_target_properties(common_utils_jpeg PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
