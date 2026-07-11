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

# htc_net_app pure-logic unit test (no syscalls; only links net_app_logic.cpp).
add_executable(test_net_app_logic
    test_net_app_logic.cpp
    ${CMAKE_SOURCE_DIR}/src/app/net_app_logic.cpp
)

target_include_directories(test_net_app_logic PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app
)

target_link_libraries(test_net_app_logic
    PRIVATE
    pthread
    rt
    gcc
    stdc++
)

set_target_properties(test_net_app_logic PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Pure wm slot scheduler unit test. This intentionally links no HAL, media,
# storage, network, or lifecycle code so scheduler behavior is locked in PC sim.
add_executable(test_wm_task_scheduler
    test_wm_task_scheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/app/workmode/wm_task_scheduler.cpp
)

target_include_directories(test_wm_task_scheduler PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/workmode
)

target_link_libraries(test_wm_task_scheduler
    PRIVATE
    pthread
    rt
    gcc
    stdc++
)

set_target_properties(test_wm_task_scheduler PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Pure um idle-timeout unit test (um-app-spec §4). Locks the idle/active decision
# kernel with injected nowMs + tiny timeouts; links no HAL/media/network code
# (um_idle.cpp compiled directly, mirroring test_wm_task_scheduler).
add_executable(test_um_idle
    test_um_idle.cpp
    ${CMAKE_SOURCE_DIR}/src/app/usermode/um_idle.cpp
)

target_include_directories(test_um_idle PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/usermode
)

target_link_libraries(test_um_idle
    PRIVATE
    pthread
    rt
    gcc
    stdc++
)

set_target_properties(test_um_idle PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# SlotOutputPort (slot1→slot2 wake signal) unit test. Locks the state machine behind
# the upload Done condition (isSettled = idle && no pending token, mutex-atomic) so the
# "no real upload before type-2 Running" + no-stranded-desc invariants stay intact.
# Header-only port, so this links no HAL/media/network code.
add_executable(test_slot_output_port
    test_slot_output_port.cpp
)

target_include_directories(test_slot_output_port PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/workmode
)

target_link_libraries(test_slot_output_port
    PRIVATE
    pthread
    rt
    gcc
    stdc++
)

set_target_properties(test_slot_output_port PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# NTP/RTC time-sync decision-logic unit test (manifest §B5, module 3).
# syncSystemTime() is not cleanly sim-testable (host clock already plausible ->
# early-outs); this locks the TIME_PLAUSIBLE gate + RTC field validation +
# graceful-failure contract + MCU implausible-in-sim behavior. Links the light
# RTC/MCU shared libs directly (NOT the heavy app_lifecycle cascade).
add_executable(test_ntp test_ntp.cpp)

target_link_libraries(test_ntp
    PRIVATE
    common_time_rtc
    mcu
    pthread
)

target_include_directories(test_ntp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/common/time/rtc
    ${CMAKE_SOURCE_DIR}/src/hardware/mcu
)

set_target_properties(test_ntp PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# HTTP control-endpoint + playback_token unit test (manifest §B6, module 7).
# Brings the real CivetWeb server up on localhost in-process and issues HTTP
# requests (handlers in http_api_v1.cpp are static — direct-call is impossible).
add_executable(test_http_api test_http_api.cpp)

target_link_libraries(test_http_api
    PRIVATE
    http_server
    camera_service
    common_misc
    logger
    jsoncpp
    civetweb
    storage
    easylogger
    pthread
)

target_include_directories(test_http_api PRIVATE
    ${CMAKE_SOURCE_DIR}/src/service/http_server
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
)

set_target_properties(test_http_api PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Async-elogy property test (elog_cfg.h ELOG_ASYNC_OUTPUT_ENABLE path). Locks
# per-call latency under an 8-thread burst, drop-oldest + drop counter, single-
# thread ordering, and drain-on-deinit (no tail loss). Links the logger lib
# (ElogInit.cpp) + easylogger (elog_async.c / elog_port.c consumer thread).
add_executable(test_elog_async test_elog_async.cpp)

target_link_libraries(test_elog_async
    PRIVATE
    logger
    easylogger
    pthread
)

target_include_directories(test_elog_async PRIVATE
    ${CMAKE_SOURCE_DIR}/src/logger
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
)

set_target_properties(test_elog_async PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# T22 wm upload SD fallback (doc/knowledge/specs/wm-upload-sd-fallback.md).
# Misc::moveDirectoryRecursive: cross-fs-safe recursive move (rename fast-path; EXDEV
# fallback copy+delete; native open/read/write, no fork-exec). Same-fs rename path only
# on PC sim (no tmpfs/vfat boundary); EXDEV branch covered on hardware (spec §8).
add_executable(test_misc_move_directory test_misc_move_directory.cpp)

target_link_libraries(test_misc_move_directory
    PRIVATE
    common_misc
    logger
    jsoncpp
    pthread
)

target_include_directories(test_misc_move_directory PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/logger
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
)

set_target_properties(test_misc_move_directory PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# T22 collectStrandedWorkDirs + isTimestampDir relaxation (^\d{8}_\d{6}(_\d+)?$):
# locks that persist's <ts>_2 collision suffix gets re-scanned on resume. Compiles
# wm_sweep.cpp directly (light deps: Misc + Logger).
add_executable(test_wm_sweep_stranded
    test_wm_sweep_stranded.cpp
    ${CMAKE_SOURCE_DIR}/src/app/workmode/wm_sweep.cpp
)

target_link_libraries(test_wm_sweep_stranded
    PRIVATE
    common_misc
    logger
    easylogger
    jsoncpp
    pthread
)

target_include_directories(test_wm_sweep_stranded PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/workmode
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/logger
    ${CMAKE_SOURCE_DIR}/third_party/jsoncpp/include
)

set_target_properties(test_wm_sweep_stranded PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
