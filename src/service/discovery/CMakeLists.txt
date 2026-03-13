set(DISCOVERY_SOURCES
    MdnsService.cpp
    MdnsTxtRecord.cpp
)

add_library(discovery_service STATIC ${DISCOVERY_SOURCES})

target_include_directories(discovery_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${THIRD_PARTY_PATH}/easylogger/inc
)

target_link_libraries(discovery_service PUBLIC
    tinysvcmdns
)
