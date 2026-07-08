set(DISCOVERY_SOURCES
    MdnsService.cpp
    MdnsTxtRecord.cpp
    MdnsParams.cpp
)

add_library(discovery_service SHARED ${DISCOVERY_SOURCES})

target_include_directories(discovery_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/misc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../config/devconf
    ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
    ${THIRD_PARTY_PATH}/easylogger/inc
)

# 输出到公共 lib/ 目录 (设备 LD_LIBRARY_PATH=./lib)
set_target_properties(discovery_service PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_link_libraries(discovery_service PUBLIC
    easylogger
    tinysvcmdns
    common_misc
    devconf
)
