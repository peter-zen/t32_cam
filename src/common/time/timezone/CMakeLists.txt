# add library
add_library(common_time_timezone SHARED Timezone.cpp)

# include header file
include_directories(
    ${SDK_INC_PATH}/systemcall
    ${SDK_INC_PATH}/sysutils
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../logger
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# link libraries
target_link_libraries(common_time_timezone PRIVATE logger)

# Set the output directory for the library
set_target_properties(common_time_timezone PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
