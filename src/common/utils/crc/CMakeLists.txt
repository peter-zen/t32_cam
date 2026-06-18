# add library
add_library(common_utils_crc SHARED CRC.cpp)

# include header file
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${THIRD_PARTY_PATH}/crc16/include
)

# link libraries
# crc16 provides cal_crc16() called by CRC.cpp; declared here at the source so
# every consumer inherits it transitively (was a latent missing-dep, T9 risk).
target_link_libraries(common_utils_crc PRIVATE logger crc16)

# Set the output directory for the library
set_target_properties(common_utils_crc PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
