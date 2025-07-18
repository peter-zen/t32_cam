# add library
add_library(mcu SHARED MCU.cpp)

# include header file
target_include_directories(mcu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(mcu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_include_directories(mcu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../time)
# link libraries
target_link_libraries(mcu PRIVATE common)

# Set the output directory for the static library
set_target_properties(mcu PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)