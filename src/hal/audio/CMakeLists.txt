cmake_minimum_required(VERSION 3.10)
project(hal_audio)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/ingenic
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
    ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
    ${SDK_INC_PATH}
    ${SDK_INC_PATH}/imp
    ${SDK_INC_PATH}/sysutils
    ${THIRD_PARTY_PATH}/faac/include
)

if(BUILD_FOR_SIMULATION)
    set(SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/stub/SimAudio.cpp
    )
else()
    set(SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/ingenic/IngenicAudio.cpp
    )
endif()

add_library(hal_audio SHARED ${SRC_FILES})

if(BUILD_FOR_SIMULATION)
    target_link_libraries(hal_audio sdk_stub logger)
else()
    target_link_libraries(hal_audio imp alog sysutils faac logger)
endif()

set_target_properties(hal_audio PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
