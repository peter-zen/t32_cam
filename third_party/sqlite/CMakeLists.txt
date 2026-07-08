add_library(sqlite3 SHARED sqlite3.c)

target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# SHARED: emit to the common lib/ dir so the device's LD_LIBRARY_PATH (./lib) finds it.
# Previously STATIC — the whole amalgamation (~843KB text) was absorbed into every
# SHARED lib that links `storage` (media_recorder/media_snap/app_workmode/app_lifecycle).
set_target_properties(sqlite3 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# SQLite Compile Options (PUBLIC: SQLITE_OMIT_*/ENABLE_* affect sqlite3.h conditional
# declarations, so every consumer must compile with the same defines).
# SQLITE_THREADSAFE=1:  Thread-safe
# SQLITE_TEMP_STORE=2:  Temp files in memory
# SQLITE_OMIT_LOAD_EXTENSION: Omit dynamic extension loading (security + size)
# SQLITE_OMIT_DEPRECATED: drop legacy APIs (size)
# Dropped SQLITE_ENABLE_RTREE: project uses plain tables only (no spatial index).
# Dropped SQLITE_ENABLE_JSON1: no-op on SQLite >= 3.38 (JSON is built-in by default).
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_THREADSAFE=1
    SQLITE_TEMP_STORE=2
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_OMIT_DEPRECATED
)

if(UNIX)
    target_link_libraries(sqlite3 PUBLIC ${CMAKE_DL_LIBS} pthread)
endif()
