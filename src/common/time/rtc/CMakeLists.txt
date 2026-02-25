# add library
add_library(common_time_rtc SHARED RTC.cpp)

# include header file
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../logger
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# link libraries
target_link_libraries(common_time_rtc PRIVATE logger)

# Set the output directory for the library
set_target_properties(common_time_rtc PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
