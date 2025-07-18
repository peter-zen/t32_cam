# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c" 
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
 )

# Include current directory headers
include_directories(
        ${SDK_INC_PATH}
        ${SDK_INC_PATH}/imp
        ${SDK_INC_PATH}/sysutils
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Create static library
add_library(media_common SHARED ${SOURCE_FILES})

# link libraries
#target_link_libraries(media_common PRIVATE media_common)

# Set the output directory for the static library
set_target_properties(media_common PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)