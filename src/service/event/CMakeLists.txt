set(EVENT_SERVICE_SOURCES
    TcpEventService.cpp
)

add_library(event_service SHARED ${EVENT_SERVICE_SOURCES})

target_include_directories(event_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
)

# 输出到公共 lib/ 目录 (设备 LD_LIBRARY_PATH=./lib)
set_target_properties(event_service PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_link_libraries(event_service PUBLIC
    jsoncpp
)
