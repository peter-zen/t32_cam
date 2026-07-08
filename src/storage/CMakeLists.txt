add_library(storage
    DatabaseManager.cpp
    MetadataDao.cpp
    MediaScanner.cpp
    StoragePaths.cpp
)

# Enable Position Independent Code for linking into shared libraries
set_property(TARGET storage PROPERTY POSITION_INDEPENDENT_CODE ON)

target_include_directories(storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/sqlite
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
)

target_link_libraries(storage PUBLIC
    sqlite3
    easylogger
)
