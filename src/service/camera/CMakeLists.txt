file(GLOB SOURCES
    "*.cpp"
    "impl/*.cpp"
)

add_library(camera_service SHARED ${SOURCES})

# 输出到公共 lib/ 目录 (设备 LD_LIBRARY_PATH=./lib)
set_target_properties(camera_service PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_include_directories(camera_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/media/snap
    ${CMAKE_SOURCE_DIR}/src/media/video
    ${CMAKE_SOURCE_DIR}/src/media/common
    ${CMAKE_SOURCE_DIR}/src/hal/include
    ${CMAKE_SOURCE_DIR}/src/media/audio
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
)

target_link_libraries(camera_service PUBLIC
    media_snap
    media_recorder
    daynight
    mcu_service
    jsoncpp
    setting
    devconf
    env
    logger
)
