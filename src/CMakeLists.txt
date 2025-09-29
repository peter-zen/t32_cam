# Include directories
include_directories(
    ${SDK_INC_PATH}
    ${SOURCES}/media/common
    ${SOURCES}/media/snap
)

# Specify the directory where the libraries are located
link_directories(
    ${SDK_LIB_PATH}
    ${CMAKE_BINARY_DIR}/lib
)

# Ensure static libraries are preferred
#set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

# Include subdirectory for common
add_subdirectory(logger)
add_subdirectory(common)
add_subdirectory(env)
add_subdirectory(devconf)
add_subdirectory(misc)
add_subdirectory(base64)
add_subdirectory(setting)
add_subdirectory(media)
add_subdirectory(network)
add_subdirectory(power)
add_subdirectory(disk)
add_subdirectory(mcu)
add_subdirectory(workmode)
add_subdirectory(daynight)   
add_subdirectory(gpio)
add_subdirectory(time)
add_subdirectory(tool)
add_subdirectory(app)
