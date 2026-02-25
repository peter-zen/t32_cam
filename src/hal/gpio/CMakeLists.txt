cmake_minimum_required(VERSION 3.10)
project(hal_gpio)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
    ${CMAKE_CURRENT_SOURCE_DIR}/ingenic
)

if(BUILD_FOR_SIMULATION)
    set(SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/stub/SimGpio.cpp
    )
else()
    set(SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/ingenic/IngenicGpio.cpp
    )
endif()

add_library(hal_gpio SHARED ${SRC_FILES})

set_target_properties(hal_gpio PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
