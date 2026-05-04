file(GLOB SOURCES
    "*.cpp"
    "impl/*.cpp"
)

add_library(system_service STATIC ${SOURCES})

target_include_directories(system_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${THIRD_PARTY_PATH}/easylogger/inc
)

target_link_libraries(system_service PUBLIC
    logger
)
