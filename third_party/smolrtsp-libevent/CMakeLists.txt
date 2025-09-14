cmake_minimum_required(VERSION 3.10.0)

file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/bufferevent.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/evbuffer.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dispatcher.c"
 )
            
# Include current directory headers
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${THIRD_PARTY_PATH}/interface99
        ${THIRD_PARTY_PATH}/datatype99
        ${THIRD_PARTY_PATH}/metalang99/include
        ${THIRD_PARTY_PATH}/slice99
        ${THIRD_PARTY_PATH}/smolrtsp/include
        ${THIRD_PARTY_PATH}/libevent/include
)

# Create library
add_library(smolrtsp-libevent SHARED ${SOURCE_FILES})

# Set the output directory
set_target_properties(smolrtsp-libevent PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
