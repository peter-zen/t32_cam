file(GLOB SOURCES "*.cpp")

add_library(mcu_service SHARED ${SOURCES})

# 输出到公共 lib/ 目录 (设备 LD_LIBRARY_PATH=./lib)
set_target_properties(mcu_service PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/hardware/mcu
    ${THIRD_PARTY_PATH}/easylogger/inc
)

target_link_libraries(mcu_service PUBLIC mcu logger)
