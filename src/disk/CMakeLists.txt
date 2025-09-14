# add library
add_library(disk SHARED Disk.cpp)

# include header file
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../logger
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# link libraries
target_link_libraries(disk PRIVATE common)

# Set the output directory for the library
set_target_properties(disk PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)