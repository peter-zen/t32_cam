file(GLOB FAAC_SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/libfaac/*.c"
)

add_library(faac SHARED ${FAAC_SOURCE_FILES})
add_library(faac::faac ALIAS faac)

set_target_properties(faac PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(faac PRIVATE
        PACKAGE_VERSION="\\\"1.0\\\""
)

target_include_directories(faac PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/libfaac"
)

target_link_libraries(faac PUBLIC m)

# Set the output directory for the library
set_target_properties(faac PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)