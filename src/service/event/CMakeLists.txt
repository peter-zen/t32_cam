set(EVENT_SERVICE_SOURCES
    TcpEventService.cpp
)

add_library(event_service STATIC ${EVENT_SERVICE_SOURCES})

target_include_directories(event_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
)

target_link_libraries(event_service PUBLIC
    jsoncpp
)
