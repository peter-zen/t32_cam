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
    ${CMAKE_SOURCE_DIR}/src/common/utils/string
    ${CMAKE_SOURCE_DIR}/third_party/sqlite
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
)

add_executable(test_minimp4_fragmented_mux test_minimp4_fragmented_mux.cpp)

target_compile_definitions(test_minimp4_fragmented_mux PRIVATE
    TEST_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(test_minimp4_fragmented_mux
    PRIVATE
    minimp4
)

target_include_directories(test_minimp4_fragmented_mux PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/minimp4/include
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

add_executable(test_mcu_service test_mcu_service.cpp)

target_link_libraries(test_mcu_service
    PRIVATE
    mcu_service
    mcu
    easylogger
    pthread
)

target_include_directories(test_mcu_service PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/hardware/mcu
    ${CMAKE_SOURCE_DIR}/src/service/mcu
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
)
