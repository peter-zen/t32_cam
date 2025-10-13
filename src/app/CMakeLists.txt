# Compile executable
file(GLOB MAIN_APP_SOURCES "${SOURCES}/app/main_app.cpp")
file(GLOB MEDIA_APP_SOURCES "${SOURCES}/app/media_app.cpp")
file (GLOB DAEMON_APP_SOURCES "${SOURCES}/app/daemon_app.cpp")

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../setting
        ${CMAKE_CURRENT_SOURCE_DIR}/../env
        ${CMAKE_CURRENT_SOURCE_DIR}/../devconf
        ${CMAKE_CURRENT_SOURCE_DIR}/../misc
        ${CMAKE_CURRENT_SOURCE_DIR}/../network
        ${CMAKE_CURRENT_SOURCE_DIR}/../mcu
        ${CMAKE_CURRENT_SOURCE_DIR}/../disk
        ${CMAKE_CURRENT_SOURCE_DIR}/../time
        ${CMAKE_CURRENT_SOURCE_DIR}/../power
        ${CMAKE_CURRENT_SOURCE_DIR}/../gpio
        ${CMAKE_CURRENT_SOURCE_DIR}/../workmode
        ${CMAKE_CURRENT_SOURCE_DIR}/../daynight
        ${CMAKE_CURRENT_SOURCE_DIR}/../daemon
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/snap
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/video
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/rtsp
        ${THIRD_PARTY_PATH}/jsoncpp/include
        ${SDK_INC_PATH}/systemcall
)

link_directories(
    ${SDK_LIB_PATH}
    ${CMAKE_BINARY_DIR}/lib
)

add_executable(htc_main_app ${MAIN_APP_SOURCES})
add_executable(htc_media_app ${MEDIA_APP_SOURCES})
add_executable(htc_daemon_app ${DAEMON_APP_SOURCES})

# Link libraries
target_link_libraries(htc_daemon_app 
    PRIVATE

    #common libs
    daemon
    misc
    common
    setting
    env
    devconf
    base64
    crc16
    md5
    jsoncpp
    daynight
    workmode
    logger

    #misc libs
    mcu
    power
    disk
    time

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
    media_rtsp

    #net libs
    network

    #common libs
    daemon
    misc
    common
    setting
    env
    devconf
    base64
    crc16
    md5
    jsoncpp
    daynight
    workmode
    logger

    #misc libs
    mcu
    power
    disk
    time

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

    #common libs
    setting
    env
    jsoncpp
    daynight
    workmode
    logger
    time
    
    #sdk libs
    system_call
    imp
    alog

    #std libs
    rt
    gcc
    stdc++
)

# Set output directory
set_target_properties(htc_main_app PROPERTIES 
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
set_target_properties(htc_media_app PROPERTIES 
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(htc_daemon_app PROPERTIES 
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install .so files to lib directory
if(SDK_LIB_PATH)
    file(GLOB SO_FILES "${SDK_LIB_PATH}/*.so")
    if(SO_FILES)
        install(FILES ${SO_FILES} DESTINATION "${CMAKE_BINARY_DIR}/lib")
    endif()
endif()