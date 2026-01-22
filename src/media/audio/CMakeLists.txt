# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/AudioRecorderFactory.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/AudioIn.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/DmicIn.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/AudioParams.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/AacEncoder.cpp"
 )

# Include current directory headers
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../../misc
        ${SDK_INC_PATH}
        ${SDK_INC_PATH}/imp
        ${SDK_INC_PATH}/sysutils
        ${SDK_INC_PATH}/samples/libimp-samples
        ${SDK_INC_PATH}/samples/libsysutils-samples
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Create library
add_library(audio_recorder SHARED ${SOURCE_FILES})

# link libraries
target_link_libraries(audio_recorder PRIVATE media_common faac::faac)

# Set the output directory for the library
set_target_properties(audio_recorder PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
