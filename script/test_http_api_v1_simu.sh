#!/bin/bash

# t32_yb SIMU HTTP API V1 quick test
# Start standalone HTTP test server and verify /api/v1/* endpoints.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_BIN="$PROJECT_ROOT/build_sim/src/service/http_server/test_http_server"
PORT="${HTTP_TEST_PORT:-8080}"
BASE_URL="http://127.0.0.1:${PORT}"
LOG_FILE="${HTTP_TEST_LOG:-$PROJECT_ROOT/sim_sdcard_runtime/logs/http_test_server.log}"

pass_count=0
fail_count=0
server_pid=""

cleanup() {
    if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
        kill "$server_pid" >/dev/null 2>&1 || true
        wait "$server_pid" 2>/dev/null || true
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

call_get() {
    local path="$1"
    curl -sS --max-time 5 "$BASE_URL$path"
}

call_post() {
    local path="$1"
    local payload="$2"
    curl -sS --max-time 5 -X POST \
        -H "Content-Type: application/json" \
        -d "$payload" \
        "$BASE_URL$path"
}

wait_server_ready() {
    for _ in $(seq 1 30); do
        if curl -sS --max-time 1 "$BASE_URL/api/health" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

extract_job_id() {
    local body="$1"
    echo "$body" | sed -n 's/.*"job_id":"\([^"]*\)".*/\1/p'
}

wait_photo_job_completed() {
    local job_id="$1"
    local resp=""
    for _ in $(seq 1 20); do
        resp="$(call_get "/api/v1/camera/photo/status?job_id=${job_id}")"
        if [[ "$resp" == *'"status":"completed"'* ]]; then
            echo "$resp"
            return 0
        fi
        sleep 0.2
    done
    echo "$resp"
    return 1
}

expect_binary_jpeg() {
    local name="$1"
    local path="$2"
    local tmp_body
    tmp_body="$(mktemp)"
    local meta
    meta="$(curl -sS --max-time 5 -o "$tmp_body" -w '%{http_code} %{content_type} %{size_download}' "$BASE_URL$path")"

    local http_code content_type size_download
    http_code="$(echo "$meta" | awk '{print $1}')"
    content_type="$(echo "$meta" | awk '{print $2}')"
    size_download="$(echo "$meta" | awk '{print $3}')"

    if [[ "$http_code" == "200" && "$content_type" == "image/jpeg" && "$size_download" -gt 0 ]]; then
        pass "$name"
    else
        fail "$name"
        echo "  expected: 200 image/jpeg size>0"
        echo "  actual: $meta"
    fi

    rm -f "$tmp_body"
}

require_cmd curl
require_cmd sed
require_cmd awk
require_cmd mktemp

if [[ ! -x "$SERVER_BIN" ]]; then
    echo "[FAIL] server not found: $SERVER_BIN"
    echo "Hint: cmake --build build_sim -j"
    exit 1
fi

mkdir -p "$(dirname "$LOG_FILE")"

if command -v fuser >/dev/null 2>&1; then
    fuser -k "${PORT}/tcp" >/dev/null 2>&1 || true
fi

echo "[INFO] starting HTTP test server"
"$SERVER_BIN" >"$LOG_FILE" 2>&1 &
server_pid=$!

if ! wait_server_ready; then
    echo "[FAIL] server not ready on $BASE_URL"
    echo "--- server log ---"
    tail -n 80 "$LOG_FILE" || true
    exit 1
fi

resp="$(call_get "/api/health")"
expect_contains "GET /api/health" "$resp" '"status":"ok"'

resp="$(call_get "/healthz")"
expect_contains "GET /healthz" "$resp" '"status":"ok"'

resp="$(call_get "/api/v1/device/info")"
expect_contains "GET /api/v1/device/info code" "$resp" '"code":0'
expect_contains "GET /api/v1/device/info model" "$resp" '"camera_model":"T32"'

resp="$(call_get "/api/v1/device/sensors")"
expect_contains "GET /api/v1/device/sensors code" "$resp" '"code":0'
expect_contains "GET /api/v1/device/sensors battery" "$resp" '"battery"'

resp="$(call_get "/api/v1/storage/info")"
expect_contains "GET /api/v1/storage/info code" "$resp" '"code":0'
expect_contains "GET /api/v1/storage/info used" "$resp" '"used":8000'

resp="$(call_post "/api/v1/system/datetime" '{"datetime":"2026-03-05T09:30:00"}')"
expect_contains "POST /api/v1/system/datetime code" "$resp" '"code":0'
expect_contains "POST /api/v1/system/datetime accepted" "$resp" '"accepted":true'

resp="$(call_post "/api/v1/system/workmode" '{"mode":1}')"
expect_contains "POST /api/v1/system/workmode code" "$resp" '"code":0'
expect_contains "POST /api/v1/system/workmode mode" "$resp" '"mode":1'

resp="$(call_post "/api/v1/storage/format" '{}')"
expect_contains "POST /api/v1/storage/format code" "$resp" '"code":0'
expect_contains "POST /api/v1/storage/format status" "$resp" '"status":"scheduled"'

resp="$(call_get "/api/v1/camera/properties")"
expect_contains "GET /api/v1/camera/properties code" "$resp" '"code":0'
expect_contains "GET /api/v1/camera/properties resolution" "$resp" '"resolution"'

resp="$(call_get "/api/v1/camera/properties/resolution")"
expect_contains "GET /api/v1/camera/properties/resolution name" "$resp" '"name":"resolution"'
expect_contains "GET /api/v1/camera/properties/resolution value" "$resp" '"value"'

resp="$(call_post "/api/v1/camera/properties" '{"fps":60,"bitrate":8192,"timestamp_overlay":false}')"
expect_contains "POST /api/v1/camera/properties code" "$resp" '"code":0'
expect_contains "POST /api/v1/camera/properties success" "$resp" '"success":3'
expect_contains "POST /api/v1/camera/properties failed" "$resp" '"failed":0'

resp="$(call_post "/api/v1/camera/properties/reset" '{"properties":["fps","bitrate","timestamp_overlay"]}')"
expect_contains "POST /api/v1/camera/properties/reset code" "$resp" '"code":0'
expect_contains "POST /api/v1/camera/properties/reset count" "$resp" '"reset_count":3'

resp="$(call_post "/api/v1/camera/photo" '{"channel":0,"save":true,"format":"jpg","quality":85,"response_mode":"sync"}')"
expect_contains "POST /api/v1/camera/photo sync code" "$resp" '"code":0'
expect_contains "POST /api/v1/camera/photo sync status" "$resp" '"status":"completed"'
expect_contains "POST /api/v1/camera/photo sync filepath" "$resp" '"filepath"'

resp="$(call_post "/api/v1/camera/photo" '{"channel":0,"save":true,"format":"jpg","quality":85,"response_mode":"async","client_request_id":"v1-probe"}')"
expect_contains "POST /api/v1/camera/photo async code" "$resp" '"code":0'
expect_contains "POST /api/v1/camera/photo async status" "$resp" '"status":"accepted"'

job_id="$(extract_job_id "$resp")"
if [[ -z "$job_id" ]]; then
    fail "Extract async photo job_id"
else
    pass "Extract async photo job_id"
    status_resp="$(wait_photo_job_completed "$job_id" || true)"
    expect_contains "GET /api/v1/camera/photo/status completed" "$status_resp" '"status":"completed"'
    expect_contains "GET /api/v1/camera/photo/status photo" "$status_resp" '"photo"'
fi

resp="$(call_post "/api/v1/camera/video/start" '{"channel":0,"duration":3,"audio":true}')"
expect_contains "POST /api/v1/camera/video/start code" "$resp" '"code":0'
expect_contains "POST /api/v1/camera/video/start status" "$resp" '"status":"recording"'

resp="$(call_get "/api/v1/camera/video/status")"
expect_contains "GET /api/v1/camera/video/status code" "$resp" '"code":0'
expect_contains "GET /api/v1/camera/video/status state" "$resp" '"status":"recording"'

resp="$(call_post "/api/v1/camera/video/stop" '{}')"
expect_contains "POST /api/v1/camera/video/stop code" "$resp" '"code":0'
expect_contains "POST /api/v1/camera/video/stop status" "$resp" '"status":"stopped"'

resp="$(call_get "/api/v1/camera/photos?offset=0&limit=5")"
expect_contains "GET /api/v1/camera/photos code" "$resp" '"code":0'
expect_contains "GET /api/v1/camera/photos field" "$resp" '"photos"'

resp="$(call_get "/api/v1/camera/video/list?offset=0&limit=5")"
expect_contains "GET /api/v1/camera/video/list code" "$resp" '"code":0'
expect_contains "GET /api/v1/camera/video/list field" "$resp" '"videos"'

expect_binary_jpeg "GET /api/v1/camera/preview" "/api/v1/camera/preview?format=jpeg"
expect_binary_jpeg "GET /api/v1/camera/thumbnail" "/api/v1/camera/thumbnail?size=160"

echo ""
echo "[SUMMARY] pass=$pass_count fail=$fail_count"
if [[ "$fail_count" -gt 0 ]]; then
    echo "[SUMMARY] FAILED"
    exit 1
fi

echo "[SUMMARY] ALL PASS"
