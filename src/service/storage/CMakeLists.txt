file(GLOB SOURCES
    "*.cpp"
    "impl/*.cpp"
)

add_library(storage_service STATIC ${SOURCES})

target_include_directories(storage_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(storage_service PUBLIC
    logger
)
