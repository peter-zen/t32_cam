cmake_minimum_required(VERSION 3.10)
project(hal_video)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/ingenic
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
    ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
    ${SDK_INC_PATH}
    ${SDK_INC_PATH}/imp
    ${SDK_INC_PATH}/sysutils
)

if(BUILD_FOR_SIMULATION)
    set(SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/stub/SimVideo.cpp
    )
else()
    set(SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/ingenic/IngenicVideo.cpp
    )
endif()

add_library(hal_video SHARED ${SRC_FILES} ../HalFactory.cpp)

if(BUILD_FOR_SIMULATION)
    target_link_libraries(hal_video PUBLIC hal_audio hal_gpio sdk_stub logger)
else()
    target_link_libraries(hal_video PUBLIC hal_audio hal_gpio imp alog sysutils logger)
endif()

set_target_properties(hal_video PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
