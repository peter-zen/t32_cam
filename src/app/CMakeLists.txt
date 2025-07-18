# Compile executable
file(GLOB MAIN_APP_SOURCES "${SOURCES}/app/main_app.cpp")
file(GLOB MEDIA_APP_SOURCES "${SOURCES}/app/media_app.cpp")

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../mcu
        ${CMAKE_CURRENT_SOURCE_DIR}/../disk
        ${CMAKE_CURRENT_SOURCE_DIR}/../time
        ${CMAKE_CURRENT_SOURCE_DIR}/../power
        ${THIRD_PARTY_PATH}/jsoncpp/include
        ${SDK_INC_PATH}/systemcall
)
# Link libraries
set(COMMON_LIBS
    common
    crc16
    md5
    jsoncpp
)

set(MEDIA_LIBS
    media_snap
)
set(NET_LIBS 
    network
)

set(MISC_LIBS
    mcu
    power
    disk
)

set(SDK_LIBS
    system_call
    imp
    alog
)

set(STD_LIBS
    rt
    gcc
    stdc++  
)

link_directories(
    ${SDK_LIB_PATH}
    ${CMAKE_BINARY_DIR}/lib
)

add_executable(htc_main_app ${MAIN_APP_SOURCES})
add_executable(htc_media_app ${MEDIA_APP_SOURCES})

# Link libraries
target_link_libraries(htc_main_app 
    PRIVATE
    ${MEDIA_LIBS}
    ${NET_LIBS}
    ${COMMON_LIBS}
    ${MISC_LIBS}
    ${SDK_LIBS}
    ${STD_LIBS}
)

target_link_libraries(htc_media_app 
    PRIVATE
    ${MEDIA_LIBS}
    ${COMMON_LIBS}
    ${SDK_LIBS}
    ${STD_LIBS}
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

# Install .so files to lib directory
if(SDK_LIB_PATH)
    file(GLOB SO_FILES "${SDK_LIB_PATH}/*.so")
    if(SO_FILES)
        install(FILES ${SO_FILES} DESTINATION "${CMAKE_BINARY_DIR}/lib")
    endif()
endif()