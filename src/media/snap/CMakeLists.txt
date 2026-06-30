# Find source files in the current directory
set(SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/ImageSnap.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/JpegEncoder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/LargeImageSnap.cpp"
)

# Include current directory headers
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hardware/daynight
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hal/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../storage
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/utils/jpeg
)

# Create library
add_library(media_snap SHARED ${SOURCE_FILES})

# link libraries - 根据编译模式选择链接库
if(BUILD_FOR_SIMULATION)
    target_link_libraries(media_snap PUBLIC hal_video sdk_stub pthread storage common_utils_jpeg)
else()
    target_link_libraries(media_snap PUBLIC hal_video storage common_utils_jpeg)
endif()
# Set the output directory for the library
set_target_properties(media_snap PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
