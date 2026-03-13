# HTTP Server 模块

set(HTTP_SERVER_SOURCES
    http_server.c
    http_api.c
    http_api_v1.cpp
    PhotoJobManager.cpp
)

add_library(http_server STATIC ${HTTP_SERVER_SOURCES})

target_include_directories(http_server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/service/camera
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
)

# 链接 CivetWeb
target_link_libraries(http_server PUBLIC
    civetweb
    camera_service
    event_service
    jsoncpp
    storage
)

# 链接 HAL (用于获取传感器数据等)
# target_link_libraries(http_server PRIVATE hal_interface)

# 测试程序 (仅在模拟模式下编译)
if(BUILD_FOR_SIMULATION)
    add_executable(test_http_server test_http_server.c)
    set_target_properties(test_http_server PROPERTIES LINKER_LANGUAGE CXX)
    target_link_libraries(test_http_server PRIVATE
        http_server
        pthread
        camera_service
        common_misc
        logger
        jsoncpp
        civetweb
        storage
    )
endif()
