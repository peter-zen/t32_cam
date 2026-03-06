#!/bin/bash

# t32_yb SIMU HTTP API quick test
# Start standalone HTTP test server and verify core endpoints.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_BIN="$PROJECT_ROOT/build_sim/src/service/http_server/test_http_server"
PORT="${HTTP_TEST_PORT:-8080}"
BASE_URL="http://127.0.0.1:${PORT}"
LOG_FILE="${HTTP_TEST_LOG:-$PROJECT_ROOT/build_sim/sdcard/logs/http_test_server.log}"

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
    curl -sS --max-time 3 "$BASE_URL$path"
}

call_post() {
    local path="$1"
    local payload="$2"
    curl -sS --max-time 3 -X POST \
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

require_cmd curl

if [[ ! -x "$SERVER_BIN" ]]; then
    echo "[FAIL] server not found: $SERVER_BIN"
    echo "Hint: cmake --build build_sim -j"
    exit 1
fi

mkdir -p "$(dirname "$LOG_FILE")"

# Free test port if already in use.
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

# GET endpoints
resp="$(call_get "/api/health")"
expect_contains "GET /api/health" "$resp" '"status":"ok"'

resp="$(call_get "/api/device/info")"
expect_contains "GET /api/device/info status" "$resp" '"status":0'
expect_contains "GET /api/device/info model" "$resp" '"camera_model"'

resp="$(call_get "/api/sensor/data")"
expect_contains "GET /api/sensor/data status" "$resp" '"status":0'
expect_contains "GET /api/sensor/data battery" "$resp" '"battery"'

resp="$(call_get "/api/params")"
expect_contains "GET /api/params" "$resp" '"param"'

resp="$(call_get "/api/storage/info")"
expect_contains "GET /api/storage/info" "$resp" '"used"'

resp="$(call_get "/api/record/status")"
expect_contains "GET /api/record/status" "$resp" '"recording"'

resp="$(call_get "/api/snapshot")"
expect_contains "GET /api/snapshot" "$resp" '"status":0'

# POST endpoints
resp="$(call_post "/api/params/set" '{"param":{"CAM_Mode":1}}')"
expect_contains "POST /api/params/set" "$resp" '"status":0'

resp="$(call_post "/api/params/reset" '{}')"
expect_contains "POST /api/params/reset" "$resp" '"status":0'

resp="$(call_post "/api/system/datetime" '{"datetime":"2026-03-05T09:30:00"}')"
expect_contains "POST /api/system/datetime" "$resp" '"status":0'

resp="$(call_post "/api/system/workmode" '{"mode":1}')"
expect_contains "POST /api/system/workmode" "$resp" '"status":0'

resp="$(call_post "/api/storage/format" '{}')"
expect_contains "POST /api/storage/format" "$resp" '"status":0'

resp="$(call_post "/api/record/start" '{}')"
expect_contains "POST /api/record/start" "$resp" '"Recording started"'

resp="$(call_post "/api/record/stop" '{}')"
expect_contains "POST /api/record/stop" "$resp" '"Recording stopped"'

echo ""
echo "[SUMMARY] pass=$pass_count fail=$fail_count"
if [[ "$fail_count" -gt 0 ]]; then
    echo "[SUMMARY] FAILED"
    exit 1
fi

echo "[SUMMARY] ALL PASS"
