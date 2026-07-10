# Compile executable
file(GLOB MAIN_APP_SOURCES "${SOURCES}/app/main_app.cpp")
file(GLOB MEDIA_APP_SOURCES "${SOURCES}/app/media_app.cpp")
file (GLOB DAEMON_APP_SOURCES "${SOURCES}/app/daemon_app.cpp")
file (GLOB NET_APP_SOURCES "${SOURCES}/app/net_app.cpp")
file(GLOB WORKMODE_APP_SOURCES "${SOURCES}/app/workmode_app.cpp")
file(GLOB WM_APP_SOURCES "${SOURCES}/app/wm_app.cpp")
file(GLOB UM_APP_SOURCES "${SOURCES}/app/um_app.cpp")
file(GLOB QUICK_SNAP_SOURCES "${SOURCES}/app/quick_snap.cpp")
set(NET_APP_HELPER_SOURCES
    "${SOURCES}/app/net_app_logic.cpp"
    "${SOURCES}/app/wifi_reconnect.cpp"
)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/misc
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/setting
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/env
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/devconf
        ${CMAKE_CURRENT_SOURCE_DIR}/../network
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/mcu
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/time
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/time/timezone
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/time/rtc
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/power
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/disk
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/gpio
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/string
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/serial
        ${CMAKE_CURRENT_SOURCE_DIR}/workmode
        ${CMAKE_CURRENT_SOURCE_DIR}/usermode
        ${CMAKE_CURRENT_SOURCE_DIR}/app_lifecycle
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/daynight
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/daemon
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/snap
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/video
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/audio
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/rtsp
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/http_server
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/camera
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/discovery
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/event
        ${CMAKE_CURRENT_SOURCE_DIR}/../storage
        ${CMAKE_CURRENT_SOURCE_DIR}/../manifest
        ${CMAKE_CURRENT_SOURCE_DIR}/../hal/include
        ${THIRD_PARTY_PATH}/jsoncpp/include
        ${THIRD_PARTY_PATH}/sqlite
        ${SDK_INC_PATH}/systemcall
)

link_directories(
    ${SDK_LIB_PATH}
    ${CMAKE_BINARY_DIR}/lib
)

add_subdirectory(workmode)
add_subdirectory(usermode)
add_subdirectory(app_lifecycle)

add_executable(htc_main_app ${MAIN_APP_SOURCES})
add_executable(htc_media_app ${MEDIA_APP_SOURCES})
add_executable(htc_daemon_app ${DAEMON_APP_SOURCES})
add_executable(htc_net_app ${NET_APP_SOURCES} ${NET_APP_HELPER_SOURCES})
add_executable(htc_workmode_app ${WORKMODE_APP_SOURCES})
add_executable(wm ${WM_APP_SOURCES})
add_executable(um ${UM_APP_SOURCES})
add_executable(quickSnap ${QUICK_SNAP_SOURCES})
add_executable(snap_test ${CMAKE_CURRENT_SOURCE_DIR}/snap_test.cpp)
add_executable(time_test ${CMAKE_CURRENT_SOURCE_DIR}/time_test.cpp)

# ============================================================================
# Platform-specific library linking
# ============================================================================
if(BUILD_FOR_SIMULATION)
    # PC模拟模式：使用stub库替代SDK库
    message(STATUS "App: Linking with SDK stub libraries for PC simulation")
    
    # htc_daemon_app
    target_link_libraries(htc_daemon_app 
        PRIVATE
        #common libs
        daemon
        common_misc
        setting
        env
        devconf
        common_utils_base64
        crc16
        md5
        jsoncpp
        daynight
        hal_audio
        hal_video
        app_workmode
        logger
        #misc libs
        mcu
        power
        disk
        common_time_rtc
        #sdk libs
        sdk_stub
        #std libs
        pthread
        rt
        gcc
        stdc++
    )

    # htc_main_app
    target_link_libraries(htc_main_app
        PRIVATE
        #storage libs
        storage
        sqlite3
        #media libs
        media_snap
        media_rtsp
        audio_recorder
        media_recorder
        hal_audio
        hal_video
        #net libs
        network
        #service libs
        http_server
        discovery_service
        event_service
        civetweb
        #common libs
        daemon
        common_misc
        common_time_timezone
        setting
        env
        devconf
        common_utils_base64
        common_utils_crc
        crc16
        md5
        jsoncpp
        daynight
        app_workmode
        app_lifecycle
        manifest
        logger
        #misc libs
        mcu
        mcu_service
        power
        disk
        common_time_rtc
        #sdk libs
        sdk_stub
        #std libs
        pthread
        rt
        gcc
        stdc++
    )

    # htc_media_app
    target_link_libraries(htc_media_app 
        PRIVATE
        #media libs
        media_snap
        hal_video
        hal_audio
        #logging
        logger
        #common libs
        setting
        env
        jsoncpp
        daynight
        app_workmode
        common_time_rtc
        common_time_timezone
        #sdk libs
        sdk_stub
        #std libs
        rt
        gcc
        stdc++
    )
    # snap_test
    target_link_libraries(snap_test
        PRIVATE
        media_snap
        daynight
        hal_audio
        hal_video
        common_misc
        gpio
        jsoncpp
        logger
        pthread
        rt
        gcc
        stdc++
    )

    # htc_net_app (SIM): links common_misc (connectWifi/isWifi*/startDHCP/
    # getIPAddress/getGatewayAddress/setNetworkInterfaceName) + mcu
    # (readUPID/readUPWD/writeUPID/writeUPWD via getInstance) + network
    # (UsbDongle: loadDriver/open/preconfig/start/setModel for --type usb).
    # network is a PRIVATE-link shared lib whose own dependencies
    # (setting/env/devconf/disk/power/common_time_*/md5/common_utils_crc/
    # common_utils_serial) do NOT propagate to this target, so we link them
    # explicitly here (mirrors the htc_main_app sim link closure for network).
    # Graceful reconnect primitives live in wifi_reconnect.cpp compiled into the
    # target (sim stubs return empty/false). No system_call needed on sim
    # (sdk_stub).
    target_link_libraries(htc_net_app
        PRIVATE
        common_misc
        mcu
        network
        setting
        env
        devconf
        disk
        common_time_rtc
        common_time_timezone
        power
        md5
        common_utils_base64
        common_utils_crc
        common_utils_serial
        crc16
        logger
        jsoncpp
        sdk_stub
        pthread
        rt
        gcc
        stdc++
    )

    # htc_workmode_app (SIM): thin -wm shell composing app_workmode +
    # app_lifecycle. Both PUBLIC-link their deps (common_misc, setting, env,
    # devconf, logger, daynight, gpio, mcu, power, storage, media_rtsp,
    # http_server, event_service, discovery_service, daemon, camera_service,
    # common_time_timezone, jsoncpp) so transitives cover most. The explicit
    # floor below is the directly-called-by-main() set + uClibc-safe parse.
    # No common_utils_string link (StringConvert.h is header-only static).
    target_link_libraries(htc_workmode_app
        PRIVATE
        app_workmode
        app_lifecycle
        common_misc          # Misc::getExecutablePath (sim StartupConfig)
        setting              # Settings::saveToJsonFile (cleanupHook)
        env                  # EnvManager
        devconf              # DeviceConfig (config alias)
        power                # Power::getInstance (invalid-arg path)
        daynight             # DayNightSwitch (cleanupHook body)
        gpio                 # GPIO / POWER_HOLD_PIN (cleanupHook body)
        mcu                  # app_lifecycle::syncWithMCU (transitive, kept explicit)
        logger
        jsoncpp              # WorkModeContext pulls jsoncpp headers
        sdk_stub             # sim SDK stub
        pthread
        rt
        gcc
        stdc++
    )

    # wm — new binary (wm-app-spec). Slice 1: m2 upload-only. Same closure as
    # htc_workmode_app (app_workmode PUBLIC-links capture/upload/time/manifest).
    target_link_libraries(wm
        PRIVATE
        app_workmode
        app_lifecycle
        common_misc
        setting
        env
        devconf
        power
        daynight
        gpio
        mcu
        logger
        jsoncpp
        sdk_stub
        pthread
        rt
        gcc
        stdc++
    )

    # um — new binary (um-app-spec). Slice 2a: server-lifecycle + idle-timeout.
    # wm closure + the long-running services um brings up (rtsp/http/mdns/event)
    # + um_idle (idle-timeout kernel). app_workmode transitively brings wm_time.
    target_link_libraries(um
        PRIVATE
        app_workmode
        app_lifecycle
        common_misc
        setting
        env
        devconf
        power
        daynight
        gpio
        mcu
        logger
        jsoncpp
        media_rtsp
        http_server
        civetweb
        discovery_service
        event_service
        um_idle
        sdk_stub
        pthread
        rt
        gcc
        stdc++
    )

    # quickSnap (SIM) — boot-phase snap + spawn downstream without shell.
    # Mirrors htc_media_app SIM closure minus system_call, plus app_lifecycle/
    # common_misc/gpio. No system_call/popen link — quickSnap uses fork+execv.
    target_link_libraries(quickSnap
        PRIVATE
        #media libs
        media_snap
        hal_video
        hal_audio
        #logging
        logger
        #common libs
        setting
        env
        jsoncpp
        daynight
        app_workmode
        gpio
        common_misc
        common_time_rtc
        common_time_timezone
        app_lifecycle
        #sdk libs
        sdk_stub
        #std libs
        rt
        gcc
        stdc++
    )
else()
# Link libraries
target_link_libraries(htc_daemon_app
    PRIVATE

    #common libs
    daemon
    common_misc
    setting
    env
    devconf
    common_utils_base64
    crc16
    md5
    jsoncpp
    daynight
    hal_audio
    hal_video
    app_workmode
    logger

    #misc libs
    mcu
    power
    disk
    gpio
    common_time_rtc

    #sdk libs
    system_call
    imp
    alog

    #std libs
    pthread
    rt
    gcc
    stdc++
)

# Link libraries
target_link_libraries(htc_main_app 
    PRIVATE

    #media libs
    media_snap
    media_recorder
    audio_recorder
    media_rtsp
    hal_audio
    hal_video

    #net libs
    network

    #service libs
    http_server
    discovery_service
    event_service
    civetweb

    #storage libs
    storage

    #common libs
    daemon
    common_time_timezone
    common_misc
    setting
    env
    devconf
    common_utils_base64
    common_utils_crc
    crc16
    md5
    jsoncpp
    daynight
    app_workmode
    app_lifecycle
    manifest
    logger

    #misc libs
    mcu
    mcu_service
    power
    disk
    gpio
    common_time_rtc

    #sdk libs
    system_call
    imp
    alog

    #std libs
    pthread
    rt
    gcc
    stdc++
)

target_link_libraries(htc_media_app 
    PRIVATE

    #media libs
    media_snap
    hal_video
    hal_audio
    #logging
    logger

    #common libs
    setting
    env
    jsoncpp
    daynight
    app_workmode
    gpio
    common_time_rtc
    common_time_timezone

    #sdk libs
    system_call
    imp
    alog

    #std libs
    rt
    gcc
    stdc++
)
target_link_libraries(snap_test
    PRIVATE
    media_snap
    daynight
    hal_audio
    hal_video
    common_misc
    gpio
    jsoncpp
    logger
    imp
    alog
    pthread
    rt
    gcc
    stdc++
)
# htc_net_app (T32): common_misc links system_call internally (for
# popencall/syscall used by wifi_reconnect); we link mcu + system_call
# explicitly to mirror daemon_app, plus network for UsbDongle (--type usb).
# network's own PRIVATE dependencies (setting/env/devconf/disk/power/
# common_time_*/md5/common_utils_crc/common_utils_serial) do not propagate to
# this target, so they are linked explicitly here (same closure as sim).
# No HAL / media deps needed.
target_link_libraries(htc_net_app
    PRIVATE
    common_misc
    mcu
    network
    setting
    env
    devconf
    disk
    common_time_rtc
    common_time_timezone
    power
    md5
    common_utils_base64
    common_utils_crc
    common_utils_serial
    crc16
    logger
    jsoncpp
    system_call
    pthread
    rt
    gcc
    stdc++
)

# htc_workmode_app (T32): same -wm shell, HW SDK. system_call replaces sdk_stub.
# app_workmode + app_lifecycle PUBLIC-link the bulk of the cascade/lifecycle
# deps (storage/media_rtsp/http_server/event_service/discovery_service/daemon/
# camera_service/common_time_timezone/daynight/gpio/mcu/power/jsoncpp/...); the
# explicit set below is the directly-called-by-main() floor + imp/alog (HW SDK
# globals pulled transitively by the cascade). Append as the T32 link demands.
target_link_libraries(htc_workmode_app
    PRIVATE
    app_workmode
    app_lifecycle
    common_misc
    setting
    env
    devconf
    power
    daynight
    gpio
    mcu
    logger
    jsoncpp
    system_call          # HW SDK (replaces sdk_stub)
    imp
    alog
    pthread
    rt
    gcc
    stdc++
)

# wm — new binary (wm-app-spec). HW closure (imp/alog/system_call replace sdk_stub).
target_link_libraries(wm
    PRIVATE
    app_workmode
    app_lifecycle
    common_misc
    setting
    env
    devconf
    power
    daynight
    gpio
    mcu
    logger
    jsoncpp
    system_call
    imp
    alog
    pthread
    rt
    gcc
    stdc++
)

# um — new binary (um-app-spec). HW closure (imp/alog/system_call + services).
target_link_libraries(um
    PRIVATE
    app_workmode
    app_lifecycle
    common_misc
    setting
    env
    devconf
    power
    daynight
    gpio
    mcu
    logger
    jsoncpp
    media_rtsp
    http_server
    civetweb
    discovery_service
    event_service
    um_idle
    system_call
    imp
    alog
    pthread
    rt
    gcc
    stdc++
)

# quickSnap (T32) — boot-phase snap + spawn downstream without shell.
# Mirrors htc_media_app T32 closure minus system_call (quickSnap uses fork+execv).
# Link app_lifecycle (for syncSystemTime); does NOT link system_call.
target_link_libraries(quickSnap
    PRIVATE
    #media libs
    media_snap
    hal_video
    hal_audio
    #logging
    logger
    #common libs
    setting
    env
    jsoncpp
    daynight
    app_workmode
    gpio
    common_misc
    common_time_rtc
    common_time_timezone
    app_lifecycle
    #sdk libs
    imp
    alog
    #std libs
    rt
    gcc
    stdc++
)
endif()

# Set output directory
set_target_properties(htc_main_app PROPERTIES 
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
set_target_properties(htc_media_app PROPERTIES 
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
set_target_properties(snap_test PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# time_test — time-chain (RTC/MCU/NTP) HW verification harness (cf. snap_test).
# No IMP/camera deps, so the link closure is platform-uniform (no SIM/HW split).
# common_misc brings ntpSyncAndWait (busybox ntpd shell) + its internal system_call.
target_link_libraries(time_test
    PRIVATE
        common_time_rtc
        mcu
        common_misc
        logger
        jsoncpp
        pthread
        rt
        gcc
        stdc++
)
set_target_properties(time_test PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(htc_daemon_app PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(htc_net_app PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(htc_workmode_app PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(wm PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
set_target_properties(um PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
set_target_properties(quickSnap PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install .so files to lib directory (only for target build)
if(NOT BUILD_FOR_SIMULATION AND SDK_LIB_PATH)
    file(GLOB SO_FILES "${SDK_LIB_PATH}/*.so")
    if(SO_FILES)
        install(FILES ${SO_FILES} DESTINATION "${CMAKE_BINARY_DIR}/lib")
    endif()
endif()

# ============================================================================
# Flatten symlinks in lib/ directory after build
# Symlinks are not supported on FAT32/exFAT SD cards used on target hardware.
# Copy the real file in place of each symlink so deployment works correctly.
# ============================================================================
if(NOT BUILD_FOR_SIMULATION)
    add_custom_command(TARGET htc_main_app POST_BUILD
        COMMAND bash "${CMAKE_SOURCE_DIR}/script/flatten_lib_symlinks.sh" "${CMAKE_BINARY_DIR}/lib"
        COMMENT "Flattening library symlinks for target deployment"
    )
endif()
