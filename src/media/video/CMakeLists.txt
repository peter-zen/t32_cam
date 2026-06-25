file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/VideoRecorder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/VideoParams.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SharedVideo.cpp"
)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/misc
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hardware/daynight
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hal/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../audio
        ${THIRD_PARTY_PATH}/minimp4/include
)

add_library(media_recorder SHARED ${SOURCE_FILES})

target_link_libraries(media_recorder PUBLIC minimp4 hal_video hal_audio audio_recorder media_fifo media_base storage daynight)

set_target_properties(media_recorder PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Diagnostic: standalone test of the SharedVideo singleton + IngenicVideoStream C++
# object path (the prime suspect for the wm cm==1 wedge). Links media_recorder (which
# brings hal_video + deps transitively). See singleton_harness.cpp + reviews/2026-06-25.
add_executable(singleton_harness ${CMAKE_CURRENT_SOURCE_DIR}/singleton_harness.cpp)
target_link_libraries(singleton_harness PRIVATE media_recorder rt pthread)
set_target_properties(singleton_harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
