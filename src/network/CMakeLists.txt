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
target_include_directories(network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_include_directories(network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../mcu)
target_include_directories(network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../disk)
target_include_directories(network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../time)
target_include_directories(network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../power)
target_include_directories(network PUBLIC ${THIRD_PARTY_PATH}/jsoncpp/include)
target_include_directories(network PUBLIC ${THIRD_PARTY_PATH}/md5/include)
# link libraries
target_link_libraries(network PRIVATE common disk mcu time power jsoncpp md5 pthread)

# Set the output directory for the static library
set_target_properties(network PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)