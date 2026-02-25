# Compile executable
file(GLOB WPA_CONN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/wpa_conn.cpp")

include_directories(
        ${SDK_INC_PATH}/systemcall
)

link_directories(
    ${SDK_LIB_PATH}
)

add_executable(wpa_conn ${WPA_CONN_SOURCES})

# Platform-specific linking
if(BUILD_FOR_SIMULATION)
    target_link_libraries(wpa_conn 
        PRIVATE
        sdk_stub
        rt
    )
else()
    target_link_libraries(wpa_conn 
        PRIVATE
        system_call
        rt
        gcc
        stdc++
    )
endif()

# Set output directory
set_target_properties(wpa_conn PROPERTIES 
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
