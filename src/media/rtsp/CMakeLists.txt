# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/RtspServer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/rtsp.c"
 )

# Include current directory headers
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/../base"
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hardware/daynight
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hal/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../audio
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${THIRD_PARTY_PATH}/interface99
        ${THIRD_PARTY_PATH}/datatype99
        ${THIRD_PARTY_PATH}/metalang99/include
        ${THIRD_PARTY_PATH}/slice99
        ${THIRD_PARTY_PATH}/smolrtsp/include
        ${THIRD_PARTY_PATH}/smolrtsp-libevent/include/
        ${THIRD_PARTY_PATH}/smolrtsp-libevent/include/smolrtsp-libevent
        ${THIRD_PARTY_PATH}/libevent/include

)


# Create library
add_library(media_rtsp SHARED ${SOURCE_FILES})

# link libraries
target_link_libraries(media_rtsp PRIVATE media_base media_fifo audio_recorder smolrtsp smolrtsp-libevent event atomic)

# 仿真模式不再依赖内置资源头，改由 SimAudio/SimVideo 合成数据

# Set the output directory for the library
set_target_properties(media_rtsp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
