# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
 )

# Include current directory headers
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../logger
)

# Create library
add_library(gpio SHARED ${SOURCE_FILES})

target_link_libraries(gpio PUBLIC logger)
# Set the output directory for the library
set_target_properties(gpio PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
