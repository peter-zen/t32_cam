file(GLOB SOURCES
    "*.cpp"
    "impl/*.cpp"
)

add_library(camera_service STATIC ${SOURCES})

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
    jsoncpp
    setting
    devconf
    env
    logger
)
