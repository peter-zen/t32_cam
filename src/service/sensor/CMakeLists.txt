file(GLOB SOURCES
    "*.cpp"
    "impl/*.cpp"
)

add_library(sensor_service STATIC ${SOURCES})

target_include_directories(sensor_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sensor_service PUBLIC
    logger
)
