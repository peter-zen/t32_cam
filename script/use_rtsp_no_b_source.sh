#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build_sim}"
BIN_RES_DIR="$BUILD_DIR/bin/res"
CONFIG_JSON="$BIN_RES_DIR/config.json"
ASSET_ROOT="${SIM_MEDIA_DIR:-$ROOT_DIR/local_assets/rtsp}"
SOURCE_H264="${SOURCE_H264:-$ASSET_ROOT/video/full_frame_camera.h264}"
SOURCE_G711A="${SOURCE_G711A:-$ASSET_ROOT/audio/full_frame_camera_g711a.alaw}"
NO_B_NAME="full_frame_camera_no_b_30s.h264"
LOCAL_NO_B_PATH="${LOCAL_NO_B_PATH:-$ASSET_ROOT/video/$NO_B_NAME}"
NO_B_PATH="$BIN_RES_DIR/$NO_B_NAME"
DEFAULT_NAME="full_frame_camera.h264"
MODE="enable"
FRAMES="${RTSP_NO_B_FRAMES:-900}"

usage() {
    cat <<'EOF'
Usage:
  script/use_rtsp_no_b_source.sh
  script/use_rtsp_no_b_source.sh --restore-default

Behavior:
  - Default: generate a no-B H.264 test source from local_assets and switch build_sim/bin/res/config.json to it
  - --restore-default: copy the original local H.264 source to build_sim/bin/res and switch config.json back to it

Env:
  BUILD_DIR          Override build directory, default: ./build_sim
  SIM_MEDIA_DIR      External RTSP media root, default: ./local_assets/rtsp
  SOURCE_H264        Override source H.264 path, default: $SIM_MEDIA_DIR/video/full_frame_camera.h264
  SOURCE_G711A       Override source G711A path, default: $SIM_MEDIA_DIR/audio/full_frame_camera_g711a.alaw
  LOCAL_NO_B_PATH    Override generated no-B path, default: $SIM_MEDIA_DIR/video/full_frame_camera_no_b_30s.h264
  RTSP_NO_B_FRAMES   Frame count for generated no-B clip, default: 900 (30s @ 30fps)
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi

if [[ "${1:-}" == "--restore-default" ]]; then
    MODE="restore"
elif [[ $# -gt 0 ]]; then
    echo "Unknown option: $1" >&2
    usage >&2
    exit 1
fi

if [[ ! -f "$CONFIG_JSON" ]]; then
    echo "Missing runtime config: $CONFIG_JSON" >&2
    echo "Build the simulation target first: cmake --build build_sim -j\$(nproc)" >&2
    exit 1
fi

copy_if_needed() {
    local src="$1"
    local dst="$2"
    local src_real=""
    local dst_real=""

    if [[ -e "$src" ]]; then
        src_real="$(readlink -f "$src")"
    fi
    if [[ -e "$dst" ]]; then
        dst_real="$(readlink -f "$dst")"
    fi

    if [[ -n "$src_real" && -n "$dst_real" && "$src_real" == "$dst_real" ]]; then
        return 0
    fi

    cp "$src" "$dst"
}

install_optional_audio() {
    if [[ -f "$SOURCE_G711A" ]]; then
        copy_if_needed "$SOURCE_G711A" "$BIN_RES_DIR/$(basename "$SOURCE_G711A")"
        echo "Installed external audio source: $(basename "$SOURCE_G711A")"
    else
        echo "External audio source not found, keep current runtime audio asset: $SOURCE_G711A"
    fi
}

if [[ "$MODE" == "restore" ]]; then
    if [[ ! -f "$SOURCE_H264" ]]; then
        echo "Missing original H.264 source: $SOURCE_H264" >&2
        exit 1
    fi
    mkdir -p "$BIN_RES_DIR"
    copy_if_needed "$SOURCE_H264" "$BIN_RES_DIR/$DEFAULT_NAME"
    install_optional_audio
    perl -0pi -e 's/"h264"\s*:\s*"[^"]+"/"h264": "full_frame_camera.h264"/' "$CONFIG_JSON"
    echo "RTSP simulation source restored to: $DEFAULT_NAME"
    echo "Start from build_sim/bin:"
    echo "  cd \"$BUILD_DIR/bin\" && ./htc_main_app -rs"
    exit 0
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ffmpeg is required to generate the no-B test source." >&2
    exit 1
fi

if [[ ! -f "$SOURCE_H264" ]]; then
    echo "Missing source asset: $SOURCE_H264" >&2
    echo "Prepare local assets under: $ASSET_ROOT" >&2
    exit 1
fi

mkdir -p "$BIN_RES_DIR" "$(dirname "$LOCAL_NO_B_PATH")"

echo "Generating local no-B RTSP source: $LOCAL_NO_B_PATH"
ffmpeg -hide_banner -loglevel error -y \
    -framerate 30 \
    -i "$SOURCE_H264" \
    -an \
    -frames:v "$FRAMES" \
    -c:v libx264 \
    -preset veryfast \
    -profile:v high \
    -level 4.0 \
    -pix_fmt yuv420p \
    -bf 0 \
    -g 30 \
    -keyint_min 30 \
    -sc_threshold 0 \
    -x264-params bframes=0:nal-hrd=none \
    -f h264 \
    "$LOCAL_NO_B_PATH"

copy_if_needed "$LOCAL_NO_B_PATH" "$NO_B_PATH"
copy_if_needed "$SOURCE_H264" "$BIN_RES_DIR/$DEFAULT_NAME"
install_optional_audio

perl -0pi -e 's/"h264"\s*:\s*"[^"]+"/"h264": "full_frame_camera_no_b_30s.h264"/' "$CONFIG_JSON"

echo "RTSP simulation source switched to: $NO_B_NAME"
echo "External media root: $ASSET_ROOT"
echo "Start from build_sim/bin:"
echo "  cd \"$BUILD_DIR/bin\" && ./htc_main_app -rs"
