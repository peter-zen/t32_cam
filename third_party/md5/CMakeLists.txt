# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/md5.c"
 )

# Include current directory headers
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
)

# Create static library
add_library(md5 SHARED ${SOURCE_FILES})

# Set the output directory for the static library
set_target_properties(md5 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)