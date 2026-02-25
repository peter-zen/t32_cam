file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/VideoRecorder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/VideoParams.cpp"
)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hardware/daynight
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hal/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../audio
        ${THIRD_PARTY_PATH}/minimp4/include
)

add_library(media_recorder SHARED ${SOURCE_FILES})

target_link_libraries(media_recorder PUBLIC minimp4 hal_video hal_audio audio_recorder media_fifo media_base storage)

set_target_properties(media_recorder PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
