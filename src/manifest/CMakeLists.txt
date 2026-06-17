# manifest library: desc-info JSON generation (extracted verbatim from main_app.cpp).
# Behavior-preserving move; singletons still read internally (decoupling = follow-up).

file(GLOB MANIFEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    # expose "manifest/Manifest.h" from the src/ root
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    # common
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/misc
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/string
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/crc
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/time/timezone
    # logger
    ${CMAKE_CURRENT_SOURCE_DIR}/../logger
    # config singletons (read-only, kept coupled)
    ${CMAKE_CURRENT_SOURCE_DIR}/../config/setting
    ${CMAKE_CURRENT_SOURCE_DIR}/../config/devconf
    # hardware singletons/helpers
    ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/mcu
    ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/disk
    # jsoncpp
    ${THIRD_PARTY_PATH}/jsoncpp/include
)

add_library(manifest SHARED ${MANIFEST_SOURCES})

# PUBLIC because the header exposes Settings/MCU/DeviceConfig/json types indirectly;
# transitive deps make the consumer (htc_main_app) link cleanly without re-listing them.
target_link_libraries(manifest
    PUBLIC
        jsoncpp
        common_misc          # Misc::*
        common_utils_crc     # CRC::calculate_crc16 (transitively provides crc16)
        common_time_timezone # Timezone::getFormattedTimeWithTimezone
        setting              # Settings::getInstance()
        devconf              # DeviceConfig::getInstance()
        mcu                  # MCU::getInstance()
        disk                 # Disk::getInfo
        logger               # Logger::log
)

set_target_properties(manifest PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
