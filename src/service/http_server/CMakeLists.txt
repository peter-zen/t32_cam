# HTTP Server 模块

set(HTTP_SERVER_SOURCES
    http_server.c
    http_api.c
)

add_library(http_server STATIC ${HTTP_SERVER_SOURCES})

target_include_directories(http_server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接 CivetWeb
target_link_libraries(http_server PUBLIC
    civetweb
)

# 链接 HAL (用于获取传感器数据等)
# target_link_libraries(http_server PRIVATE hal_interface)

# 测试程序 (仅在模拟模式下编译)
if(BUILD_FOR_SIMULATION)
    add_executable(test_http_server test_http_server.c)
    target_link_libraries(test_http_server PRIVATE
        http_server
        pthread
    )
endif()
