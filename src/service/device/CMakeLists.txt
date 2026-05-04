file(GLOB SOURCES
    "*.cpp"
    "impl/*.cpp"
)

add_library(device_service STATIC ${SOURCES})

target_include_directories(device_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(device_service PUBLIC
    logger
)
