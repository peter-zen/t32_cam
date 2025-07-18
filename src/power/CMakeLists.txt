# add library
add_library(power SHARED Power.cpp)

# include header file
target_include_directories(power PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(power PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# link libraries
target_link_libraries(power PRIVATE common)

# Set the output directory for the static library
set_target_properties(power PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)