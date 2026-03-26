if(NOT DEFINED SIM_LOCAL_ASSET_DIR)
    message(FATAL_ERROR "SIM_LOCAL_ASSET_DIR is required")
endif()

if(NOT DEFINED BIN_RES_DIR)
    message(FATAL_ERROR "BIN_RES_DIR is required")
endif()

file(MAKE_DIRECTORY "${BIN_RES_DIR}")
file(MAKE_DIRECTORY "${SIM_LOCAL_ASSET_DIR}/video")
file(MAKE_DIRECTORY "${SIM_LOCAL_ASSET_DIR}/audio")

set(SRC_FULL_H264 "${SIM_LOCAL_ASSET_DIR}/video/full_frame_camera.h264")
set(SRC_NO_B_H264 "${SIM_LOCAL_ASSET_DIR}/video/full_frame_camera_no_b_30s.h264")
set(SRC_G711A "${SIM_LOCAL_ASSET_DIR}/audio/full_frame_camera_g711a.alaw")

function(copy_optional src dst)
    if(NOT EXISTS "${src}")
        message(STATUS "[sim-assets] missing optional asset: ${src}")
        return()
    endif()

    get_filename_component(src_real "${src}" REALPATH)
    if(EXISTS "${dst}")
        get_filename_component(dst_real "${dst}" REALPATH)
        if(src_real STREQUAL dst_real)
            message(STATUS "[sim-assets] skip same-file copy: ${src}")
            return()
        endif()
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${src}" "${dst}"
        RESULT_VARIABLE copy_result
    )
    if(copy_result EQUAL 0)
        message(STATUS "[sim-assets] prepared ${dst}")
    else()
        message(WARNING "[sim-assets] failed to copy ${src} -> ${dst}")
    endif()
endfunction()

copy_optional("${SRC_FULL_H264}" "${BIN_RES_DIR}/full_frame_camera.h264")
copy_optional("${SRC_G711A}" "${BIN_RES_DIR}/full_frame_camera_g711a.alaw")

if(NOT EXISTS "${SRC_NO_B_H264}" AND EXISTS "${SRC_FULL_H264}")
    find_program(FFMPEG_EXECUTABLE ffmpeg)
    if(FFMPEG_EXECUTABLE)
        message(STATUS "[sim-assets] generating no-B source from ${SRC_FULL_H264}")
        execute_process(
            COMMAND "${FFMPEG_EXECUTABLE}"
                    -hide_banner
                    -loglevel error
                    -y
                    -framerate 30
                    -i "${SRC_FULL_H264}"
                    -an
                    -frames:v 900
                    -c:v libx264
                    -preset veryfast
                    -profile:v high
                    -level 4.0
                    -pix_fmt yuv420p
                    -bf 0
                    -g 30
                    -keyint_min 30
                    -sc_threshold 0
                    -x264-params bframes=0:nal-hrd=none
                    -f h264
                    "${SRC_NO_B_H264}"
            RESULT_VARIABLE ffmpeg_result
            ERROR_VARIABLE ffmpeg_error
        )
        if(ffmpeg_result EQUAL 0)
            message(STATUS "[sim-assets] generated ${SRC_NO_B_H264}")
        else()
            message(WARNING "[sim-assets] failed to generate no-B source: ${ffmpeg_error}")
        endif()
    else()
        message(STATUS "[sim-assets] ffmpeg not found, skip no-B generation")
    endif()
endif()

copy_optional("${SRC_NO_B_H264}" "${BIN_RES_DIR}/full_frame_camera_no_b_30s.h264")
