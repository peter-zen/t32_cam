# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c" 
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
 )

# Include EasyLogger headers
include_directories(
        ${THIRD_PARTY_PATH}/easylogger/inc
)

# Create library
add_library(logger SHARED ${SOURCE_FILES})

# Link with EasyLogger
target_link_libraries(logger PRIVATE easylogger)

# Set the output directory for the library
set_target_properties(logger PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
