file(GLOB SOURCES "*.cpp")

add_library(mcu_service STATIC ${SOURCES})

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/hardware/mcu
    ${THIRD_PARTY_PATH}/easylogger/inc
)

target_link_libraries(mcu_service PUBLIC mcu logger)
