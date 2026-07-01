# EasyLogger CMake configuration
# Supports both PC simulation and target hardware compilation

# Source files
set(ELOG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/elog.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/elog_async.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/elog_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/elog_port.c
)

# Include directories
set(ELOG_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

# Create static library
add_library(easylogger STATIC ${ELOG_SOURCES})

# Enable position independent code for linking with shared libraries
set_target_properties(easylogger PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Set include directories
target_include_directories(easylogger PUBLIC ${ELOG_INCLUDE_DIRS})

# Platform-specific settings
if(BUILD_FOR_SIMULATION)
    target_compile_definitions(easylogger PRIVATE BUILD_FOR_SIMULATION)
endif()

# Link pthread for thread safety
find_package(Threads REQUIRED)
target_link_libraries(easylogger PRIVATE Threads::Threads)

# Set output directory
set_target_properties(easylogger PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
