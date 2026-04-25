#!/bin/bash

# Focused SIMU test for fMP4 HTTP playback metadata and Range serving.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_BIN="$PROJECT_ROOT/build_sim/src/service/http_server/test_http_server"
PORT="${HTTP_TEST_PORT:-18080}"
BASE_URL="http://127.0.0.1:${PORT}"
WORK_ROOT="${PLAYBACK_TEST_ROOT:-}"
OWN_WORK_ROOT=0
LOG_FILE=""
server_pid=""
pass_count=0
fail_count=0

cleanup() {
    if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
        kill "$server_pid" >/dev/null 2>&1 || true
        for _ in $(seq 1 20); do
            if ! kill -0 "$server_pid" 2>/dev/null; then
                break
            fi
            sleep 0.1
        done
        kill -9 "$server_pid" >/dev/null 2>&1 || true
        wait "$server_pid" 2>/dev/null || true
    fi
    if [[ "$OWN_WORK_ROOT" -eq 1 && -n "$WORK_ROOT" ]]; then
        rm -rf "$WORK_ROOT"
    fi
}
trap cleanup EXIT

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[FAIL] Missing command: $1"
        exit 1
    fi
}

pass() {
    pass_count=$((pass_count + 1))
    echo "[PASS] $1"
}

fail() {
    fail_count=$((fail_count + 1))
    echo "[FAIL] $1"
}

expect_contains() {
    local name="$1"
    local body="$2"
    local needle="$3"
    if [[ "$body" == *"$needle"* ]]; then
        pass "$name"
    else
        fail "$name"
        echo "  expected contains: $needle"
        echo "  actual: $body"
    fi
}

http_code() {
    local path="$1"
    shift
    curl --noproxy '*' -sS --max-time 5 "$@" -o /dev/null -w '%{http_code}' "$BASE_URL$path"
}

call_get() {
    local path="$1"
    curl --noproxy '*' -sS --max-time 5 "$BASE_URL$path"
}

wait_server_ready() {
    for _ in $(seq 1 40); do
        if curl --noproxy '*' -sS --max-time 1 "$BASE_URL/api/health" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

expect_status() {
    local name="$1"
    local path="$2"
    local expected="$3"
    shift 3

    local actual
    actual="$(http_code "$path" "$@" 2>/dev/null || true)"
    if [[ "$actual" == "$expected" ]]; then
        pass "$name"
    else
        fail "$name"
        echo "  expected HTTP $expected"
        echo "  actual HTTP $actual"
    fi
}

expect_video_download() {
    local name="$1"
    local path="$2"
    local expected_code="$3"
    local expected_size="$4"
    shift 4

    local tmp_body tmp_headers meta
    tmp_body="$(mktemp)"
    tmp_headers="$(mktemp)"
    meta="$(curl --noproxy '*' -sS --max-time 5 "$@" -D "$tmp_headers" -o "$tmp_body" \
        -w '%{http_code} %{content_type} %{size_download}' "$BASE_URL$path" 2>/dev/null || true)"

    local http_code_value content_type size_download
    http_code_value="$(echo "$meta" | awk '{print $1}')"
    content_type="$(echo "$meta" | awk '{print $2}')"
    size_download="$(echo "$meta" | awk '{print $3}')"

    local size_ok=0
    if [[ "$expected_size" == "gt0" && "$size_download" =~ ^[0-9]+$ && "$size_download" -gt 0 ]]; then
        size_ok=1
    elif [[ "$size_download" == "$expected_size" ]]; then
        size_ok=1
    fi

    if [[ "$http_code_value" == "$expected_code" && "$content_type" == "video/mp4" && "$size_ok" -eq 1 ]]; then
        pass "$name"
    else
        fail "$name"
        echo "  expected: $expected_code video/mp4 size=$expected_size"
        echo "  actual: $meta"
        echo "  headers:"
        sed -n '1,20p' "$tmp_headers"
    fi

    rm -f "$tmp_body" "$tmp_headers"
}

expect_media_download() {
    local name="$1"
    local path="$2"
    local expected_code="$3"
    local expected_content_type="$4"
    local expected_size="$5"
    shift 5

    local tmp_body tmp_headers meta
    tmp_body="$(mktemp)"
    tmp_headers="$(mktemp)"
    meta="$(curl --noproxy '*' -sS --max-time 5 "$@" -D "$tmp_headers" -o "$tmp_body" \
        -w '%{http_code} %{content_type} %{size_download}' "$BASE_URL$path" 2>/dev/null || true)"

    local http_code_value content_type size_download
    http_code_value="$(echo "$meta" | awk '{print $1}')"
    content_type="$(echo "$meta" | awk '{print $2}')"
    size_download="$(echo "$meta" | awk '{print $3}')"

    local size_ok=0
    if [[ "$expected_size" == "gt0" && "$size_download" =~ ^[0-9]+$ && "$size_download" -gt 0 ]]; then
        size_ok=1
    elif [[ "$size_download" == "$expected_size" ]]; then
        size_ok=1
    fi

    if [[ "$http_code_value" == "$expected_code" && "$content_type" == "$expected_content_type" && "$size_ok" -eq 1 ]]; then
        if grep -qi '^Content-Disposition: attachment;' "$tmp_headers"; then
            pass "$name"
        else
            fail "$name"
            echo "  missing Content-Disposition attachment header"
            sed -n '1,20p' "$tmp_headers"
        fi
    else
        fail "$name"
        echo "  expected: $expected_code $expected_content_type size=$expected_size"
        echo "  actual: $meta"
        echo "  headers:"
        sed -n '1,20p' "$tmp_headers"
    fi

    rm -f "$tmp_body" "$tmp_headers"
}

quote_sql() {
    local value="$1"
    value="${value//\'/\'\'}"
    printf "'%s'" "$value"
}

require_cmd curl
require_cmd sqlite3
require_cmd grep
require_cmd awk
require_cmd mktemp
require_cmd dd

if [[ ! -x "$SERVER_BIN" ]]; then
    echo "[FAIL] server not found: $SERVER_BIN"
    echo "Hint: cmake --build build_sim -j"
    exit 1
fi

if [[ -z "$WORK_ROOT" ]]; then
    WORK_ROOT="$(mktemp -d /tmp/t32_playback_http.XXXXXX)"
    OWN_WORK_ROOT=1
else
    rm -rf "$WORK_ROOT"
    mkdir -p "$WORK_ROOT"
fi

mkdir -p "$WORK_ROOT/DCIM" "$WORK_ROOT/data/db" "$WORK_ROOT/logs"
LOG_FILE="$WORK_ROOT/logs/http_playback_test_server.log"

sample="$WORK_ROOT/DCIM/playback_sample.mp4"
old_sample="$WORK_ROOT/DCIM/old_normal.mp4"
outside_sample="$WORK_ROOT/outside.mp4"
symlink_sample="$WORK_ROOT/DCIM/link.mp4"
photo_sample="$WORK_ROOT/DCIM/photo_sample.jpg"

{
    printf '\x00\x00\x00\x18ftypisom\x00\x00\x02\x00isomiso6mp41'
    printf '\x00\x00\x00\x08moov'
    printf '\x00\x00\x00\x08moof'
    printf '\x00\x00\x08\x00mdat'
    dd if=/dev/zero bs=1 count=2040 2>/dev/null
} > "$sample"
cp "$sample" "$old_sample"
cp "$sample" "$outside_sample"
ln -s "$sample" "$symlink_sample"
printf '\xff\xd8\xff\xe0JFIF\x00\x01\x02\x00\x00\x01\x00\x01\x00\x00\xff\xd9' > "$photo_sample"

sample_size="$(stat -c%s "$sample")"
old_size="$(stat -c%s "$old_sample")"
outside_size="$(stat -c%s "$outside_sample")"
symlink_size="$sample_size"
photo_size="$(stat -c%s "$photo_sample")"
now_ts="$(date +%s)"

sqlite3 "$WORK_ROOT/data/db/media_file.db" <<SQL
CREATE TABLE media_files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    file_path TEXT NOT NULL UNIQUE,
    type INTEGER NOT NULL,
    timestamp INTEGER NOT NULL,
    file_size INTEGER NOT NULL,
    duration INTEGER DEFAULT 0,
    width INTEGER DEFAULT 0,
    height INTEGER DEFAULT 0,
    is_favorite INTEGER DEFAULT 0,
    is_locked INTEGER DEFAULT 0,
    container_type TEXT DEFAULT '',
    playback_capable INTEGER DEFAULT 0,
    playback_reason TEXT DEFAULT '',
    playback_token TEXT DEFAULT '',
    range_supported INTEGER DEFAULT 0,
    seek_support TEXT DEFAULT '',
    seek_granularity_ms INTEGER DEFAULT 0,
    effective_gop_frames INTEGER DEFAULT 0,
    effective_gop_ms INTEGER DEFAULT 0,
    fragment_index_path TEXT DEFAULT ''
);
CREATE INDEX idx_media_time ON media_files(timestamp DESC);
CREATE INDEX idx_media_type ON media_files(type);
CREATE INDEX idx_media_type_time ON media_files(type, timestamp DESC);
CREATE INDEX idx_media_playback ON media_files(type, playback_capable);
CREATE INDEX idx_media_playback_token ON media_files(playback_token);
INSERT INTO media_files
    (id, file_path, type, timestamp, file_size, duration, width, height,
     container_type, playback_capable, playback_token, range_supported,
     seek_support, seek_granularity_ms, effective_gop_frames, effective_gop_ms)
VALUES
    (1, $(quote_sql "$sample"), 2, $now_ts, $sample_size, 2, 320, 180,
     'fmp4', 1, 'token-playback-ok', 1, 'keyframe', 1000, 15, 1000);
INSERT INTO media_files
    (id, file_path, type, timestamp, file_size, duration, width, height,
     container_type, playback_capable, playback_reason)
VALUES
    (2, $(quote_sql "$old_sample"), 2, $((now_ts - 1)), $old_size, 2, 320, 180,
     '', 0, 'old_mp4_not_supported');
INSERT INTO media_files
    (id, file_path, type, timestamp, file_size, duration, width, height,
     container_type, playback_capable, range_supported, seek_support)
VALUES
    (3, $(quote_sql "$outside_sample"), 2, $((now_ts - 2)), $outside_size, 2, 320, 180,
     'fmp4', 1, 1, 'keyframe');
INSERT INTO media_files
    (id, file_path, type, timestamp, file_size, duration, width, height,
     container_type, playback_capable, range_supported, seek_support)
VALUES
    (4, $(quote_sql "$symlink_sample"), 2, $((now_ts - 3)), $symlink_size, 2, 320, 180,
     'fmp4', 1, 1, 'keyframe');
INSERT INTO media_files
    (id, file_path, type, timestamp, file_size, duration, width, height,
     playback_token)
VALUES
    (5, $(quote_sql "$photo_sample"), 1, $((now_ts - 4)), $photo_size, 0, 64, 64,
     'token-photo-ok');
SQL

sqlite3 "$WORK_ROOT/data/db/media_thumb.db" <<SQL
CREATE TABLE thumbnails (
    file_path TEXT PRIMARY KEY,
    data BLOB
);
SQL

if ! grep -a -q "moov" "$sample" || ! grep -a -q "moof" "$sample" || ! grep -a -q "mdat" "$sample"; then
    echo "[FAIL] generated sample is missing fMP4 box markers"
    exit 1
fi
pass "Generated HTTP playback fixture contains moov/moof/mdat markers"

SIM_SD_ROOT="$WORK_ROOT" HTTP_TEST_PORT="$PORT" "$SERVER_BIN" >"$LOG_FILE" 2>&1 &
server_pid=$!

if ! wait_server_ready; then
    fail "server ready"
    echo "--- server log ---"
    tail -n 120 "$LOG_FILE" || true
    exit 1
fi
pass "server ready"

resp="$(call_get "/api/v1/camera/video/list?offset=0&limit=10")"
expect_contains "Video list code" "$resp" '"code":0'
expect_contains "Video list has playback metadata" "$resp" '"playback_capable":true'
expect_contains "Video list has playback URL" "$resp" '"playback_url":"/api/v1/camera/video/playback?id=1"'
expect_contains "Video list has download URL" "$resp" '"download_url":"/api/v1/camera/files/download?id=1"'
expect_contains "Old row remains disabled" "$resp" '"playback_reason":"old_mp4_not_supported"'

photo_resp="$(call_get "/api/v1/camera/photos?offset=0&limit=10")"
expect_contains "Photo list code" "$photo_resp" '"code":0'
expect_contains "Photo list has download URL" "$photo_resp" '"download_url":"/api/v1/camera/files/download?id=5"'

expect_status "Missing selector rejected" "/api/v1/camera/video/playback" "400"
expect_status "Raw path selector rejected" "/api/v1/camera/video/playback?path=/etc/passwd" "400"
expect_status "Overflow id rejected" "/api/v1/camera/video/playback?id=2147483648" "400"
expect_status "Unknown id rejected" "/api/v1/camera/video/playback?id=9999" "404"
expect_status "Disabled old video rejected" "/api/v1/camera/video/playback?id=2" "415"
expect_status "Outside-root video rejected" "/api/v1/camera/video/playback?id=3" "403"
expect_status "Symlink video rejected" "/api/v1/camera/video/playback?id=4" "403"

expect_video_download "Full GET by id" "/api/v1/camera/video/playback?id=1" "200" "gt0"
expect_video_download "Full GET by token" "/api/v1/camera/video/playback?token=token-playback-ok" "200" "gt0"
expect_video_download "Range GET first kilobyte" "/api/v1/camera/video/playback?id=1" "206" "1024" -H "Range: bytes=0-1023"
expect_media_download "Video download by id" "/api/v1/camera/files/download?id=1" "200" "video/mp4" "gt0"
expect_media_download "Video download by token" "/api/v1/camera/files/download?token=token-playback-ok" "200" "video/mp4" "gt0"
expect_media_download "Photo original download by id" "/api/v1/camera/files/download?id=5" "200" "image/jpeg" "$photo_size"
expect_media_download "Photo original download by token" "/api/v1/camera/files/download?token=token-photo-ok" "200" "image/jpeg" "$photo_size"

head_code="$(http_code "/api/v1/camera/video/playback?id=1" -I 2>/dev/null || true)"
if [[ "$head_code" == "200" ]]; then
    pass "HEAD playback"
else
    fail "HEAD playback"
    echo "  expected HTTP 200"
    echo "  actual HTTP $head_code"
fi

echo ""
echo "[SUMMARY] pass=$pass_count fail=$fail_count"
if [[ "$fail_count" -gt 0 ]]; then
    echo "[SUMMARY] FAILED"
    echo "--- server log ---"
    tail -n 120 "$LOG_FILE" || true
    exit 1
fi

echo "[SUMMARY] ALL PASS"
