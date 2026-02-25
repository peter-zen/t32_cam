# add library
add_library(mcu SHARED MCU.cpp IIC.cpp)

# include header file
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/utils/string
    ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/utils/string

    ${CMAKE_CURRENT_SOURCE_DIR}
)
# link libraries
target_link_libraries(mcu PUBLIC logger)

# Set the output directory for the library
set_target_properties(mcu PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
