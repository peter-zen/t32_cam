# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
 )

# Include current directory headers
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../gpio
)

# Create library
add_library(workmode SHARED ${SOURCE_FILES})

target_link_libraries(workmode PUBLIC gpio logger)

# Set the output directory for the library
set_target_properties(workmode PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
