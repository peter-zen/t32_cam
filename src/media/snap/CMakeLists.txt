# Find source files in the current directory
set(SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/ImageSnap.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/JpegEncoder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/LargeImageSnap.cpp"
)

# Include current directory headers
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hardware/daynight
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hal/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../storage
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/utils/jpeg
        ${CMAKE_CURRENT_SOURCE_DIR}/../video      # SharedVideo.h (进程级 IngenicVideo 单例)
)

# Create library
add_library(media_snap SHARED ${SOURCE_FILES})

# link libraries - 根据编译模式选择链接库
if(BUILD_FOR_SIMULATION)
    target_link_libraries(media_snap PUBLIC hal_video sdk_stub pthread storage common_utils_jpeg)
else()
    target_link_libraries(media_snap PUBLIC hal_video storage common_utils_jpeg)
endif()
# media_recorder: SharedVideo.cpp（进程级 IngenicVideo 单例）所在 lib，ImageSnap 共用之。
# PRIVATE：media_snap.so 运行时 DT_NEEDED 它（单例符号经此解析），但不传播给 consumer
# （snap_test 等）——避免把 media_recorder 的整个 closure（DayNightSwitch 等 undefined）
# 拖进 snap_test 的链接检查。
target_link_libraries(media_snap PRIVATE media_recorder)

# Set the output directory for the library
set_target_properties(media_snap PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
