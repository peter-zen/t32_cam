add_executable(test_database test_database.cpp)

target_link_libraries(test_database
    PRIVATE
    storage
    sqlite3
    easylogger
    pthread
    dl
)

target_include_directories(test_database PRIVATE
    ${CMAKE_SOURCE_DIR}/src/storage
    ${CMAKE_SOURCE_DIR}/third_party/sqlite
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
)
