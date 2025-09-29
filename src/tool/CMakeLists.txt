# Compile executable
file(GLOB WPA_CONN_SOURCES "${SOURCES}/tool/wpa_conn.cpp")

include_directories(
        ${SDK_INC_PATH}/systemcall
)

link_directories(
    ${SDK_LIB_PATH}
)

add_executable(wpa_conn ${WPA_CONN_SOURCES})

# Link libraries
target_link_libraries(wpa_conn 
    PRIVATE

    #sdk libs
    system_call

    #std libs
    rt
    gcc
    stdc++
)

# Set output directory
set_target_properties(wpa_conn PROPERTIES 
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)