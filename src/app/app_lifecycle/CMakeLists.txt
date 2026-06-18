# app_lifecycle — process lifecycle (signal machinery + common startup + main_exit
# tail) shared by htc_main_app and the future htc_workmode_app. Extracted
# verbatim from src/app/main_app.cpp (Phase C-1, T14).
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
     )

# Include directories needed by ProcessLifecycle.cpp + the public header.
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..                      # app.h (path defines)
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common            # Common.h (pins)
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils         # AutoRelease.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../logger               # Logger + ElogInit + elog_i
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/devconf       # DeviceConfig
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/env           # EnvManager
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/setting       # Settings
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/daynight    # DayNightSwitch
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/gpio        # GPIO
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/power       # Power
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/misc          # Misc
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/time/timezone # Timezone (S13)
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/daemon       # registerToDaemonServer (S10)
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/discovery    # MdnsService (shutdown transport)
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/event        # TcpEventService (shutdown transport)
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/http_server  # http_server_* (shutdown transport)
        ${CMAKE_CURRENT_SOURCE_DIR}/../storage              # MediaScanner (S3) + DatabaseManager (S2)
        ${CMAKE_CURRENT_SOURCE_DIR}/../../media/rtsp        # RtspServer::getInstance()->shutdown() (R1)
        ${THIRD_PARTY_PATH}/jsoncpp/include                 # Json (factory-config importer, S11)
)

add_library(app_lifecycle SHARED ${SOURCE_FILES})

target_link_libraries(app_lifecycle PUBLIC
        common_misc            # Misc (mountSDCard/createDirectory/...)
        setting                # Settings
        env                    # EnvManager
        devconf                # DeviceConfig
        logger                 # Logger + ElogInit + elog_i
        daynight               # DayNightSwitch
        gpio                   # GPIO
        power                  # Power::getInstance()->isChangeModeRequested()
        common_time_timezone   # Timezone (S13)
        http_server            # http_server_is_running/stop/deinit
        event_service          # TcpEventService (shutdown transport)
        discovery_service      # MdnsService (shutdown transport)
        storage                # MediaScanner (S3) + DatabaseManager (S2)
        media_rtsp             # RtspServer::getInstance()->shutdown() (R1 gate)
        camera_service         # CameraFactoryConfigImporter (HW S11 factory-config)
        daemon                 # registerToDaemonServer (S10)
        jsoncpp                # Json (factory-config importer)
        pthread
        rt
        gcc
        stdc++
)

set_target_properties(app_lifecycle PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
