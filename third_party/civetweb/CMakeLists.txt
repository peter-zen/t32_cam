# CivetWeb - 嵌入式 HTTP Server
# https://github.com/civetweb/civetweb

cmake_minimum_required(VERSION 3.10)

# CivetWeb 源文件
set(CIVETWEB_SOURCES
    src/civetweb.c
)

# 创建动态库 (was STATIC — 103KB 被 app_workmode + app_lifecycle 各吸收一份；现在全局唯一)
add_library(civetweb SHARED ${CIVETWEB_SOURCES})

# 头文件路径
target_include_directories(civetweb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 输出到公共 lib/ 目录 (设备 LD_LIBRARY_PATH=./lib)
set_target_properties(civetweb PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# 编译选项 - 嵌入式裁剪
target_compile_definitions(civetweb PRIVATE
    NO_CGI=1                    # 禁用 CGI
    NO_CACHING=1                # 禁用缓存
    NO_SSL=1                    # 禁用 SSL (节省空间)
    USE_IPV6=0                  # 禁用 IPv6
    NO_FILES=0                  # 保留文件服务功能
)

# 链接 pthread
if(NOT WIN32)
    target_link_libraries(civetweb PRIVATE pthread)
endif()

# 设置 C 标准
set_target_properties(civetweb PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
