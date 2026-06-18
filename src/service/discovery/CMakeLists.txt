set(DISCOVERY_SOURCES
    MdnsService.cpp
    MdnsTxtRecord.cpp
    MdnsParams.cpp
)

add_library(discovery_service STATIC ${DISCOVERY_SOURCES})

target_include_directories(discovery_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/misc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../config/devconf
    ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
    ${THIRD_PARTY_PATH}/easylogger/inc
)

target_link_libraries(discovery_service PUBLIC
    easylogger
    tinysvcmdns
    common_misc
    devconf
)
