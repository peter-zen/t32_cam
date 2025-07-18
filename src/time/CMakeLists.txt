# add library
add_library(time SHARED RTC.cpp)

# include header file
target_include_directories(time PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(time PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# link libraries
target_link_libraries(time PRIVATE common)

# Set the output directory for the static library
set_target_properties(time PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)