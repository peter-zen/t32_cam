# add library
add_library(power SHARED Power.cpp)

# include header file
include_directories(
        ${SDK_INC_PATH}/imp
        ${SDK_INC_PATH}/systemcall
        ${SDK_INC_PATH}/sysutils
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../misc
        ${CMAKE_CURRENT_SOURCE_DIR}/../setting
        ${THIRD_PARTY_PATH}/crc16/include
        ${THIRD_PARTY_PATH}/jsoncpp/include
)

# link libraries
target_link_libraries(power PRIVATE common setting logger misc)

# Set the output directory for the library
set_target_properties(power PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)