cmake_minimum_required(VERSION 3.10)
project(hal)
set(CMAKE_CXX_STANDARD 11)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ingenic
    ${CMAKE_CURRENT_SOURCE_DIR}/simu
    ${CMAKE_CURRENT_SOURCE_DIR}/../logger
    ${SDK_INC_PATH}
    ${SDK_INC_PATH}/imp
    ${SDK_INC_PATH}/sysutils
    ${THIRD_PARTY_PATH}/faac/include
    ${THIRD_PARTY_PATH}/jsoncpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../config/setting
)

if(BUILD_FOR_SIMULATION)
    set(HAL_AUDIO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/simu/SimAudio.cpp)
    set(HAL_VIDEO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/simu/SimVideo.cpp)
    set(HAL_GPIO_SRC  ${CMAKE_CURRENT_SOURCE_DIR}/simu/SimGpio.cpp)
else()
    set(HAL_AUDIO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/ingenic/IngenicAudio.cpp)
    set(HAL_VIDEO_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/ingenic/IngenicVideo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ingenic/IspOsdManager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ingenic/ResidentChannelMap.cpp
    )
    set(HAL_GPIO_SRC  ${CMAKE_CURRENT_SOURCE_DIR}/ingenic/IngenicGpio.cpp)
endif()

add_library(hal_audio SHARED ${HAL_AUDIO_SRC})
add_library(hal_gpio  SHARED ${HAL_GPIO_SRC})
add_library(hal_video SHARED ${HAL_VIDEO_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/HalProvider.cpp)

if(BUILD_FOR_SIMULATION)
    target_link_libraries(hal_audio sdk_stub logger)
    target_link_libraries(hal_gpio  sdk_stub logger)
    target_link_libraries(hal_video PUBLIC hal_audio hal_gpio sdk_stub logger)
    set(SIM_LOCAL_ASSET_DIR "${PROJECT_ROOT}/local_assets/rtsp")
    add_custom_command(TARGET hal_video POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/simu/res
                ${CMAKE_BINARY_DIR}/bin/res
        COMMAND ${CMAKE_COMMAND}
                -DSIM_LOCAL_ASSET_DIR=${SIM_LOCAL_ASSET_DIR}
                -DBIN_RES_DIR=${CMAKE_BINARY_DIR}/bin/res
                -P ${CMAKE_CURRENT_SOURCE_DIR}/PrepareSimLocalAssets.cmake
    )
else()
    target_link_libraries(hal_audio imp alog sysutils faac logger)
    target_link_libraries(hal_gpio  imp alog sysutils logger)
    target_link_libraries(hal_video PUBLIC hal_audio hal_gpio imp alog sysutils logger)
    target_link_libraries(hal_video PRIVATE setting)
endif()

set_target_properties(hal_audio PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
set_target_properties(hal_gpio PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
set_target_properties(hal_video PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
