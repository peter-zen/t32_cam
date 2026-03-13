cmake_minimum_required(VERSION 3.10)

set(TINYSVCMDNS_SOURCES
    mdns.c
    mdnsd.c
)

add_library(tinysvcmdns STATIC ${TINYSVCMDNS_SOURCES})

target_include_directories(tinysvcmdns PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinysvcmdns PRIVATE c_std_99)

if(NOT WIN32)
    target_link_libraries(tinysvcmdns PUBLIC pthread)
endif()

set_target_properties(tinysvcmdns PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
