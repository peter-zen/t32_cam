# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/minimp4.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/minimp4_test.c"
 )

# Include current directory headers
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
)

# Create  library
add_library(minimp4 SHARED ${SOURCE_FILES})
#add_executable(minimp4_test ${SOURCE_FILES})

# Set the output directory
set_target_properties(minimp4 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)