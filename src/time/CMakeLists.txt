# add library
add_library(time SHARED RTC.cpp)

# include header file
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../logger
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# link libraries
target_link_libraries(time PRIVATE common)

# Set the output directory for the library
set_target_properties(time PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)