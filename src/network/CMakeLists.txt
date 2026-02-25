# add library
# 注意: RemoteCtrlClient 已移除，功能由 HTTP Server API 替代
add_library(network SHARED
    Client.cpp
    Broadcast.cpp
    FirmwareUpdate.cpp
    MgmtServClient.cpp
    StorageServClient.cpp
    UsbDongle.cpp
    Rtmp.cpp
)

# include header file
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../config/setting
    ${CMAKE_CURRENT_SOURCE_DIR}/../config/env
    ${CMAKE_CURRENT_SOURCE_DIR}/../config/devconf
    ${CMAKE_CURRENT_SOURCE_DIR}/../logger
    ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/mcu
    ${CMAKE_CURRENT_SOURCE_DIR}/../hardware
    ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/disk
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/time
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/time/timezone
    ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/power
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDK_INC_PATH}/systemcall
    ${THIRD_PARTY_PATH}/jsoncpp/include
    ${THIRD_PARTY_PATH}/md5/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/misc
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/string
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/serial
)

# link libraries
target_link_libraries(network PRIVATE setting env disk mcu common_time_rtc common_time_timezone power jsoncpp md5 pthread common_utils_base64 common_misc common_utils_serial)

# Set the output directory for the library
set_target_properties(network PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
