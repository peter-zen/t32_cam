#!/bin/bash

# t32_yb SIMU legacy HTTP API removal check
# Verify all retired legacy business endpoints now return 404.

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
require_cmd mktemp

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

expect_404() {
    local method="$1"
    local path="$2"
    local payload="${3:-}"
    local tmp_body
    tmp_body="$(mktemp)"

    local http_code
    if [[ "$method" == "POST" ]]; then
        http_code="$(curl -sS --max-time 3 -o "$tmp_body" -w '%{http_code}' -X POST \
            -H "Content-Type: application/json" \
            -d "$payload" \
            "$BASE_URL$path")"
    else
        http_code="$(curl -sS --max-time 3 -o "$tmp_body" -w '%{http_code}' "$BASE_URL$path")"
    fi

    local body
    body="$(cat "$tmp_body")"
    rm -f "$tmp_body"

    if [[ "$http_code" == "404" && "$body" == *'"error":"Not Found"'* && "$body" == *"\"path\":\"$path\""* ]]; then
        pass "$method $path returns 404"
    else
        fail "$method $path returns 404"
        echo "  expected: 404 with not-found payload"
        echo "  actual code: $http_code"
        echo "  actual body: $body"
    fi
}

expect_404 "GET" "/api/device/info"
expect_404 "GET" "/api/sensor/data"
expect_404 "POST" "/api/system/datetime" '{"datetime":"2026-03-05T09:30:00"}'
expect_404 "POST" "/api/system/workmode" '{"mode":1}'
expect_404 "GET" "/api/storage/info"
expect_404 "POST" "/api/storage/format" '{}'

expect_404 "GET" "/api/params"
expect_404 "POST" "/api/params/set" '{"param":{"CAM_Mode":1}}'
expect_404 "POST" "/api/params/reset" '{}'
expect_404 "GET" "/api/record/status"
expect_404 "POST" "/api/record/start" '{}'
expect_404 "POST" "/api/record/stop" '{}'
expect_404 "GET" "/api/snapshot"

echo ""
echo "[SUMMARY] pass=$pass_count fail=$fail_count"
if [[ "$fail_count" -gt 0 ]]; then
    echo "[SUMMARY] FAILED"
    exit 1
fi

echo "[SUMMARY] ALL PASS"
