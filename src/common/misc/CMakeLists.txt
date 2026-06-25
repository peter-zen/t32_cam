# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c" 
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
 )

# Include current directory headers
include_directories(
        ${SDK_INC_PATH}/imp
        ${SDK_INC_PATH}/systemcall
        ${SDK_INC_PATH}/sysutils
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../utils/string
        ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
        ${THIRD_PARTY_PATH}/crc16/include
        ${THIRD_PARTY_PATH}/jsoncpp/include
)

# Create library
add_library(common_misc SHARED ${SOURCE_FILES})

# Platform-specific linking
# jsoncpp: Misc.cpp 用 Json（parseFromStream/CharReaderBuilder 等）——common_misc.so 须自包含。
if(BUILD_FOR_SIMULATION)
    target_link_libraries(common_misc PRIVATE sdk_stub crc16 pthread jsoncpp)
else()
    target_link_libraries(common_misc PRIVATE system_call crc16 pthread jsoncpp)
endif()

# Set of output directory for the library
set_target_properties(common_misc PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
