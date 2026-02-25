# Compile executable
file(GLOB MAIN_APP_SOURCES "${SOURCES}/app/main_app.cpp")
file(GLOB MEDIA_APP_SOURCES "${SOURCES}/app/media_app.cpp")
file (GLOB DAEMON_APP_SOURCES "${SOURCES}/app/daemon_app.cpp")

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
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
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/daynight
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/daemon
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/snap
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/video
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/audio
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/rtsp
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/http_server
        ${CMAKE_CURRENT_SOURCE_DIR}/../storage
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

add_executable(htc_main_app ${MAIN_APP_SOURCES})
add_executable(htc_media_app ${MEDIA_APP_SOURCES})
add_executable(htc_daemon_app ${DAEMON_APP_SOURCES})
add_executable(snap_test ${CMAKE_CURRENT_SOURCE_DIR}/snap_test.cpp)

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

set_target_properties(htc_daemon_app PROPERTIES 
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
