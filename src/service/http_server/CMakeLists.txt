# HTTP Server 模块

set(HTTP_SERVER_SOURCES
    http_server.c
    http_api_v1.cpp
    PhotoJobManager.cpp
    ${CMAKE_SOURCE_DIR}/src/service/ServiceProvider.cpp
)

add_library(http_server SHARED ${HTTP_SERVER_SOURCES})

# 输出到公共 lib/ 目录 (设备 LD_LIBRARY_PATH=./lib)；device/sensor/storage/system_service
# 为本库私有依赖 (仅 http_server 用)，保持 STATIC 吸收进本 .so
set_target_properties(http_server PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_include_directories(http_server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/service/camera
    ${CMAKE_SOURCE_DIR}/src/service
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
)

# 链接 CivetWeb
target_link_libraries(http_server PUBLIC
    civetweb
    camera_service
    event_service
    device_service
    sensor_service
    storage_service
    system_service
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
