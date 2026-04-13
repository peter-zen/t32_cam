add_executable(test_database test_database.cpp)

target_link_libraries(test_database
    PRIVATE
    storage
    sqlite3
    easylogger
    pthread
    dl
)

target_include_directories(test_database PRIVATE
    ${CMAKE_SOURCE_DIR}/src/storage
    ${CMAKE_SOURCE_DIR}/third_party/sqlite
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
)

add_executable(test_camera_properties test_camera_properties.cpp)

target_compile_definitions(test_camera_properties PRIVATE
    TEST_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(test_camera_properties
    PRIVATE
    camera_service
    setting
    devconf
    env
    jsoncpp
    logger
    easylogger
    pthread
    dl
)

target_include_directories(test_camera_properties PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
)

add_executable(test_mdns_txt_record test_mdns_txt_record.cpp)

target_link_libraries(test_mdns_txt_record
    PRIVATE
    discovery_service
)

add_executable(test_mdns_model_normalization test_mdns_model_normalization.cpp)

target_link_libraries(test_mdns_model_normalization
    PRIVATE
    discovery_service
)

add_executable(test_tcp_event_service test_tcp_event_service.cpp)

target_link_libraries(test_tcp_event_service
    PRIVATE
    event_service
    easylogger
    pthread
)

target_include_directories(test_tcp_event_service PRIVATE
    ${CMAKE_SOURCE_DIR}/src/service/event
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
)
