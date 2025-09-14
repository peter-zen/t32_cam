# add library
add_library(network SHARED
    Client.cpp
    Broadcast.cpp
    FirmwareUpdate.cpp
    MgmtServClient.cpp
    RemoteCtrlClient.cpp
    StorageServClient.cpp
    Usb4gDongle.cpp
    Rtmp.cpp
)

# include header file
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../setting
    ${CMAKE_CURRENT_SOURCE_DIR}/../env
    ${CMAKE_CURRENT_SOURCE_DIR}/../devconf
    ${CMAKE_CURRENT_SOURCE_DIR}/../base64
    ${CMAKE_CURRENT_SOURCE_DIR}/../misc
    ${CMAKE_CURRENT_SOURCE_DIR}/../logger
    ${CMAKE_CURRENT_SOURCE_DIR}/../mcu
    ${CMAKE_CURRENT_SOURCE_DIR}/../disk
    ${CMAKE_CURRENT_SOURCE_DIR}/../time
    ${CMAKE_CURRENT_SOURCE_DIR}/../power
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${THIRD_PARTY_PATH}/jsoncpp/include
    ${THIRD_PARTY_PATH}/md5/include
)

# link libraries
target_link_libraries(network PRIVATE common setting env disk mcu time power jsoncpp md5 pthread)

# Set the output directory for the library
set_target_properties(network PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)