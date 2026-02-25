# add library
add_library(common_utils_crc SHARED CRC.cpp)

# include header file
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${THIRD_PARTY_PATH}/crc16/include
)

# link libraries
target_link_libraries(common_utils_crc PRIVATE logger)

# Set the output directory for the library
set_target_properties(common_utils_crc PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
