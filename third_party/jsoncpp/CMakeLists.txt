# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/json_reader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/json_writer.cpp" 
        "${CMAKE_CURRENT_SOURCE_DIR}/src/json_value.cpp"
 )

# Include current directory headers
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Create library
add_library(jsoncpp SHARED ${SOURCE_FILES})

# Set the output directory for the library
set_target_properties(jsoncpp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)