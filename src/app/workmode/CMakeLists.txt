# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
 )

# Include directories needed by WorkModeRunner.cpp (the moved cascade + 5
# helpers) + the public header. Mirrors main_app.cpp's include block, since
# WorkModeRunner.cpp IS the cascade extracted from main_app.cpp (T15).
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}                          # WorkModeRunner.h, RtspWorkMode.h, WorkMode.h
        ${CMAKE_CURRENT_SOURCE_DIR}/..                       # app.h (path defines: MEDIA_TARGET_PATH etc.)
        ${CMAKE_CURRENT_SOURCE_DIR}/../app_lifecycle         # ProcessLifecycle.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../common                # Common.h (pins)
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/misc           # Misc
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils          # AutoRelease.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/string   # StringConvert (to_string_custom/stoi_custom)
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/crc      # utils/crc/CRC.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/time/rtc       # time/rtc/RTC.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/devconf        # DeviceConfig
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/env            # EnvManager
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/setting        # Settings
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware              # PTYPE_*, INI_SECTION/KEY, GPIO_VALUE
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/daynight     # DayNightSwitch / DayNightState
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/gpio         # GPIO
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/disk         # Disk (helpers)
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/power        # Power (runWorkMode invalid-mode + upload)
        ${CMAKE_CURRENT_SOURCE_DIR}/../network               # MgmtServClient, StorageServClient, UsbDongle
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/discovery     # MdnsService, MdnsParams, service::buildMdnsParams/isMdnsEnabled
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/event         # TcpEventService
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/http_server   # http_server_*
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/camera        # CameraRecorder, RecordingPostProcess (processCmdVideoRecord)
        ${CMAKE_CURRENT_SOURCE_DIR}/../storage               # MetadataDao, DatabaseManager, MediaScanner
        ${CMAKE_CURRENT_SOURCE_DIR}/../../media/snap         # ImageSnap, ImageSnapParams
        ${CMAKE_CURRENT_SOURCE_DIR}/../../media/video        # VideoRecorder, VideoParams, CameraRecorder, Recording-postProcess
        ${CMAKE_CURRENT_SOURCE_DIR}/../../media/audio        # AudioRecorder, AudioParams
        ${CMAKE_CURRENT_SOURCE_DIR}/../../media/rtsp         # RtspServer, DEFAULT_RTSP_PORT
        ${CMAKE_CURRENT_SOURCE_DIR}/../../manifest           # manifest/Manifest.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../logger                # Logger + ElogInit + elog_i
        ${THIRD_PARTY_PATH}/jsoncpp/include                  # json/json.h
)

# Create library
add_library(app_workmode SHARED ${SOURCE_FILES})

# The cascade + 5 helpers (WorkModeRunner.cpp) pull the bulk of main_app's
# link surface. Declared PUBLIC so htc_main_app (which links app_workmode)
# gets them transitively. T10/T13 lesson: declare deps at the source TU;
# T32 link is the gate (R7).
target_link_libraries(app_workmode PUBLIC
        # existing (WorkMode.cpp + RtspWorkMode.cpp)
        mcu gpio logger media_rtsp
        # NEW — cascade + 5 helpers
        app_lifecycle              # ProcessLifecycle (ctx.lc)
        common_misc                # Misc (connectWifi/startDHCP/ntpSyncAndWait/getIPAddress/...)
        network                    # MgmtServClient, StorageServClient, UsbDongle
        storage                    # MetadataDao, DatabaseManager, MediaScanner
        media_snap                 # ImageSnap (processCmdSnap)
        media_recorder             # VideoRecorder, CameraRecorder, Recording-postProcess
        audio_recorder             # AudioRecorder, AudioParams
        http_server                # http_server_init/start/stop/deinit/is_running (CMD_MOBILE)
        event_service              # TcpEventService (CMD_MOBILE)
        discovery_service          # MdnsService, service::buildMdnsParams/isMdnsEnabled (CMD_MOBILE)
        setting                    # Settings (cameraMode)
        env                        # EnvManager
        devconf                    # DeviceConfig
        common_time_rtc            # RTC::getInstance() (CMD_GET_RTC/SET_RTC/NTP)
        common_utils_crc           # utils/crc/CRC.h (helpers)
        crc16                      # (helpers — main_app links it)
        power                      # Power (runWorkMode invalid-mode + upload)
        disk                       # Disk (helpers)
        daynight                   # DayNightSwitch / DayNightState
        jsoncpp                    # json/json.h (CMD_UPLOAD)
        civetweb                   # transitive of http_server on T32
        manifest                   # manifest/Manifest.h (helpers)
        pthread
        rt
        gcc
        stdc++
)

# Set the output directory for the library
set_target_properties(app_workmode PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
