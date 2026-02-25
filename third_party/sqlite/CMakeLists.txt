add_library(sqlite3 STATIC sqlite3.c)

target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Enable Position Independent Code for linking into shared libraries
set_property(TARGET sqlite3 PROPERTY POSITION_INDEPENDENT_CODE ON)

# SQLite Compile Options
# SQLITE_THREADSAFE=1:  Thread-safe
# SQLITE_TEMP_STORE=2:  Temp files in memory
# SQLITE_OMIT_LOAD_EXTENSION: Omit dynamic extension loading (security + size)
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_THREADSAFE=1
    SQLITE_TEMP_STORE=2
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_ENABLE_RTREE
    SQLITE_ENABLE_JSON1
)

if(UNIX)
    target_link_libraries(sqlite3 PUBLIC ${CMAKE_DL_LIBS} pthread)
endif()
