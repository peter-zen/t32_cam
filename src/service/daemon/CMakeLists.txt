# CMakeLists.txt for daemon module
# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/DeamonServer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/DeamonClient.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/daemon_api.cpp"
 )

# Include current directory headers
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/setting
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/env
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/gpio
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/mcu
)

# Create library
add_library(daemon SHARED ${SOURCE_FILES})

target_link_libraries(daemon PUBLIC logger pthread)
# Set the output directory for the library
set_target_properties(daemon PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
