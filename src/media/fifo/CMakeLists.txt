cmake_minimum_required(VERSION 3.10)

file(GLOB FIFO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/MediaFIFO.cpp"
)

file(GLOB FIFO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/MediaFIFO.h"
)

add_library(media_fifo STATIC ${FIFO_SOURCES} ${FIFO_HEADERS})
set_property(TARGET media_fifo PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(media_fifo PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_CURRENT_SOURCE_DIR}/../base
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
