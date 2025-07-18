# add library
add_library(disk SHARED Disk.cpp)

# include header file
target_include_directories(disk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(disk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# link libraries
target_link_libraries(disk PRIVATE common)

# Set the output directory for the static library
set_target_properties(disk PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)