# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c" 
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
 )

# Include current directory headers
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
        ${SDK_INC_PATH}
        ${SDK_INC_PATH}/imp
        ${SDK_INC_PATH}/sysutils
        ${SDK_INC_PATH}/samples/libimp-samples
        ${SDK_INC_PATH}/samples/libsysutils-samples
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Create library
add_library(media_snap SHARED ${SOURCE_FILES})

# link libraries
target_link_libraries(media_snap PRIVATE media_common)

# Set the output directory for the library
set_target_properties(media_snap PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)