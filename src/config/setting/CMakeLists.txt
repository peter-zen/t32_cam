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
        ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
        ${THIRD_PARTY_PATH}/crc16/include
        ${THIRD_PARTY_PATH}/jsoncpp/include
)

# Create library
add_library(setting SHARED ${SOURCE_FILES})

target_link_libraries(setting PRIVATE jsoncpp)

# Set the output directory for the library
set_target_properties(setting PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
